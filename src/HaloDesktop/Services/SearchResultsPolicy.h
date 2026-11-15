#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace HaloDesktop::Services
{
    // How a search across several addons' catalogs becomes one list, ported
    // from the reference client's shared `search/results.ts`, plus the
    // desktop's own `libraryMatches` and `suggestedSearches`. These rules hold
    // no UI or WinRT types in their interface so the tests compile them by path.

    // Short enough to feel live while typing, long enough not to ask every
    // addon per keystroke.
    inline constexpr std::uint32_t SearchDebounceMilliseconds = 250;
    inline constexpr std::size_t SearchMinimumQuery = 2;
    // No title is longer, and the server caps a catalog's search text well above it.
    inline constexpr std::size_t SearchMaximumQuery = 120;
    // At most this many recent searches, and only as many as fit on one line.
    inline constexpr std::size_t SearchRecentShown = 6;
    // Enough popular titles to browse without a search, short of a whole catalog.
    inline constexpr std::size_t SearchPopularShown = 24;
    inline constexpr std::size_t SearchSuggestionLimit = 5;

    // One catalog entry as the search rules read it: the addon's fields as the
    // mappers validated them. An empty optional and an empty string both mean
    // the addon sent nothing, as a falsy value does in the reference.
    struct SearchTitle final
    {
        std::wstring Id;
        std::wstring Type;
        std::wstring Name;
        std::optional<std::wstring> Poster;
        std::optional<std::wstring> Background;
        std::optional<std::wstring> Description;
        std::optional<std::wstring> ReleaseInfo;
        std::optional<std::wstring> Rating;
        std::optional<std::wstring> Logo;
        std::optional<std::wstring> DefaultVideoId;
        // `imdb_id`, sent beside an addon's own id.
        std::optional<std::wstring> ImdbId;

        bool operator==(SearchTitle const&) const = default;
    };

    // One searchable catalog: an installed addon's catalog that takes a
    // `search` extra and needs nothing else.
    struct SearchTarget final
    {
        std::wstring Key;
        std::wstring AddonId;
        std::wstring AddonName;
        std::wstring Type;
        std::wstring Id;
        // The catalog's own name, the row's title.
        std::wstring Title;
    };

    enum class SearchTargetStatus
    {
        Pending,
        Failed,
        Done,
    };

    // Where one catalog's answer stands.
    struct SearchTargetState final
    {
        SearchTargetStatus Status{ SearchTargetStatus::Pending };
        std::vector<SearchTitle> Titles;
    };

    // One result row: a catalog that answered with titles.
    struct SearchResultGroup final
    {
        std::wstring Key;
        std::wstring Title;
        std::wstring AddonName;
        std::wstring Type;
        std::vector<SearchTitle> Titles;
    };

    struct SearchProgressResult final
    {
        // Rows so far, in addon order; a catalog's row appears as soon as it answers.
        std::vector<SearchResultGroup> Groups;
        // The best guess for the query (see SearchProgress). Never swaps once shown.
        std::optional<SearchTitle> TopMatch;
        // Catalogs still out.
        std::size_t Pending{};
        // Catalogs that failed, for naming the addons that did not answer.
        std::vector<SearchTarget> Failed;
        // Catalogs asked.
        std::size_t Total{};
    };

    // Lower case without accents, so "Amélie" is found by "amelie": canonical
    // decomposition, combining marks removed, then lower case, as the
    // reference's `text.normalize('NFD').replace(/\p{M}/gu, '').toLowerCase()`.
    [[nodiscard]] std::wstring FoldedText(std::wstring_view text);
    // Lower case only, as JavaScript's `toLowerCase`.
    [[nodiscard]] std::wstring LowerText(std::wstring_view text);
    // JavaScript's `String.prototype.trim`.
    [[nodiscard]] std::wstring TrimmedText(std::wstring_view text);
    // What the search box keeps of what was typed: the first 120 UTF-16 units,
    // without splitting a surrogate pair.
    [[nodiscard]] std::wstring CappedQuery(std::wstring_view text);
    // Whether a query, trimmed, is long enough to search.
    [[nodiscard]] bool IsSearchableQuery(std::wstring_view query);

    // `type:id`, the identity two addons share for one title.
    [[nodiscard]] std::wstring TitleKey(SearchTitle const& title);
    // A catalog's titles without repeats; addons sometimes list one title twice.
    [[nodiscard]] std::vector<SearchTitle> UniqueTitles(std::vector<SearchTitle> titles);

    // How well a title's name answers the query, lower is better: the name is
    // the query (0), starts with its words (1), starts with it mid-word (2),
    // holds every word of it (3), holds every word inside longer words (4),
    // holds some of them (5), or none (6).
    [[nodiscard]] int NameRelevance(std::wstring_view name, std::wstring_view query);
    // The same titles, best-named for the query first; stable within a level.
    [[nodiscard]] std::vector<SearchTitle> ByRelevance(std::vector<SearchTitle> titles, std::wstring_view query);

    // Every identity a title can be recognised by: `type:id`, its IMDb id
    // (its own or `imdb_id`), and its kind, folded name and year together.
    [[nodiscard]] std::vector<std::wstring> TitleIdentities(SearchTitle const& title);
    // Several ranked lists as one, taking turns, each title once even when two
    // addons name it by different ids; the better-ranked copy stays and gains
    // what the other knew. Titles shown elsewhere are passed in `exclude`.
    [[nodiscard]] std::vector<SearchTitle> Interleave(
        std::span<std::vector<SearchTitle> const> lists,
        std::span<SearchTitle const> exclude = {});
    // The merged results as the screen shows them: what was asked for first,
    // and without what nobody asked for. `missingArt` holds the keys of titles
    // whose poster is known not to load beyond what `HasNoPoster` admits.
    [[nodiscard]] std::vector<SearchTitle> RefineResults(
        std::span<SearchTitle const> titles,
        std::wstring_view query,
        std::unordered_set<std::wstring> const& missingArt = {});

    // Whether an addon's answer already says the title has no poster: none
    // sent, a TMDB link built from a missing image, or a card the addon draws
    // in its place (MediaFusion's `mf…` entries, by link or by id).
    [[nodiscard]] bool HasNoPoster(std::optional<std::wstring> const& poster, std::optional<std::wstring_view> id = std::nullopt);
    [[nodiscard]] bool HasNoPoster(SearchTitle const& title);
    // The first four-digit year of a release line, or none.
    [[nodiscard]] std::optional<std::wstring> ReleaseYear(std::optional<std::wstring> const& releaseInfo);

    // What the search screen shows from each catalog's state, as answers
    // arrive. With a query the top match is the best-named first title of the
    // answering catalogs, the earlier catalog on a tie; while catalogs are out
    // only an exact name with no earlier catalog still out is shown. Without a
    // query it is the first title of the first catalog that answered with any,
    // undecided while an earlier catalog is still out.
    [[nodiscard]] SearchProgressResult SearchProgress(
        std::span<SearchTarget const> targets,
        std::span<SearchTargetState const> states,
        std::optional<std::wstring_view> query = std::nullopt);

    // One library entry, as `libraryMatches` reads it.
    struct SearchLibraryEntry final
    {
        // `type:metaId`.
        std::wstring Id;
        std::wstring Type;
        std::wstring Name;
        std::optional<std::wstring> Poster;
        std::int64_t AddedAt{};
        bool Removed{};
    };

    // The library entries the query names (every folded word of it in the
    // folded name), newest first, as titles.
    [[nodiscard]] std::vector<SearchTitle> LibraryMatches(
        std::span<SearchLibraryEntry const> library,
        std::wstring_view query);

    // Past searches that contain what is being typed, most recent first; the
    // term itself is not suggested back.
    [[nodiscard]] std::vector<std::wstring> SuggestedSearches(
        std::span<std::wstring const> history,
        std::wstring_view typed,
        std::size_t limit = SearchSuggestionLimit);

    // The note over a search some catalogs did not answer: every one failed,
    // or the addons that did not, named once each in order.
    [[nodiscard]] std::wstring SearchFailureNote(std::span<SearchTarget const> failed, std::size_t total);
    // "Still asking N more catalog(s)…".
    [[nodiscard]] std::wstring SearchStillAskingNote(std::size_t pending);
}
