#pragma once

#include "ViewModels/HomeStatePolicy.h"

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
    // The pure rules behind Home's rows, ported from the reference client's
    // `home/homeRows.ts` so both desktop clients title and pick the same shelves.

    // The most catalog shelves Home draws, the most titles one shelf shows, and
    // the most titles the recently watched shelf shows.
    inline constexpr std::size_t MaximumHomeShelves = 16;
    inline constexpr std::size_t HomeShelfLimit = 30;
    inline constexpr std::size_t RecentlyWatchedLimit = 15;

    // Only films and series belong on Home: addons also publish account
    // listings under other types (TorBox's torrent list is `other`), whose
    // entries are release names and hashes, not titles to browse.
    [[nodiscard]] bool IsHomeCatalogType(std::wstring_view type) noexcept;

    struct ShelfLabels final
    {
        std::wstring Title;
        std::wstring Source;
    };

    // A catalog shelf's title (the catalog's name with its kind, "Popular
    // Movies") and the mono source line beside it (the addon, "CINEMETA"). An
    // addon's own prefix ("TMDB - Latest Releases") moves to the source line,
    // as its short form when it is an acronym; a name that already says its
    // kind is kept; one ending on a plural noun takes the kind in front
    // ("Latest Movie Releases"); no name is titled by its kind.
    [[nodiscard]] ShelfLabels CatalogShelfLabels(
        std::optional<std::wstring_view> catalogName,
        std::wstring_view type,
        std::wstring_view addonName);

    // One watch-history row, reduced to what the recently watched shelf reads.
    struct RecentlyWatchedRow final
    {
        std::wstring ItemId;
        std::optional<std::wstring> Name;
        std::optional<std::wstring> Poster;
        std::int64_t UpdatedAt{};
    };

    // One library entry, keyed by the same `type:metaId` item id.
    struct RecentlyWatchedLibraryEntry final
    {
        std::wstring Id;
        std::wstring Name;
        std::optional<std::wstring> Poster;
        bool Removed{};
    };

    struct RecentlyWatchedTitle final
    {
        std::wstring Type;
        std::wstring MetaId;
        std::wstring Name;
        std::optional<std::wstring> Poster;

        bool operator==(RecentlyWatchedTitle const&) const = default;
    };

    // The last distinct titles played, newest first, at any progress. Titles
    // the continue shelf already offers (`exclude`, by item id) are left out,
    // the row's own name and poster come before the library entry's, a row
    // nothing names is dropped, and the filter applies before the limit.
    [[nodiscard]] std::vector<RecentlyWatchedTitle> BuildRecentlyWatchedRow(
        std::vector<RecentlyWatchedRow> rows,
        std::span<RecentlyWatchedLibraryEntry const> library,
        std::unordered_set<std::wstring> const& exclude,
        ::HaloDesktop::ViewModels::HomeFilter filter,
        std::size_t limit = RecentlyWatchedLimit);

    // Home's kind filter applied to a raw type: anything that is not a series
    // counts as a film.
    [[nodiscard]] bool MatchesHomeFilterType(
        ::HaloDesktop::ViewModels::HomeFilter filter,
        std::wstring_view type) noexcept;
}
