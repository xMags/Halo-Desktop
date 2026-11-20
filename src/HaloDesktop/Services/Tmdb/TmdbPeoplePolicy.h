#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace HaloDesktop::Services::Tmdb
{
    // TMDB's answers, reduced to bounded, validated fields, and the Halo
    // server's own answers built from them. A port of the server's
    // `backend/src/halo/tmdb.ts` and `closest-people.ts`: the same limits, the
    // same filtering and the same answer shapes, so the app reads a local
    // answer exactly as it reads the server's (Api/PeopleMappers.cpp).
    // Every TMDB answer is untrusted: anything of the wrong type or out of
    // bounds is dropped, never trusted.

    inline constexpr std::size_t MaximumText = 200;
    inline constexpr std::size_t MaximumBiography = 2000;
    inline constexpr std::size_t MaximumSearchPeople = 8;
    inline constexpr std::size_t MaximumKnownForTitles = 3;
    // The most credits a person page lists, before mapping to IMDb ids.
    inline constexpr std::size_t MaximumPersonCredits = 60;

    enum class TitleType
    {
        Movie,
        Series,
    };

    enum class CreditRole
    {
        Cast,
        Director,
        Writer,
    };

    struct PersonSummary final
    {
        std::int64_t TmdbId{};
        std::wstring Name;
        // `Acting`, `Directing` or `Writing`.
        std::optional<std::wstring> KnownFor;
        std::optional<std::wstring> Photo;
        double Popularity{};
        std::vector<std::wstring> KnownForTitles;
    };

    struct TitleHit final
    {
        TitleType Type{ TitleType::Movie };
        std::int64_t TmdbId{};
        std::wstring Name;
        std::optional<std::wstring> Year;
        std::optional<std::wstring> Poster;
        double Popularity{};
    };

    // One title a person is credited on, roles merged, before its IMDb id is known.
    struct CreditCandidate final
    {
        TitleHit Title;
        // Cast before director before writer.
        std::vector<CreditRole> Roles;
        std::optional<std::wstring> Character;
    };

    struct PersonDetails final
    {
        std::int64_t TmdbId{};
        std::wstring Name;
        std::optional<std::wstring> Photo;
        std::optional<std::wstring> KnownFor;
        std::optional<std::wstring> Biography;
        // At most MaximumPersonCredits, most popular first.
        std::vector<CreditCandidate> Credits;
    };

    struct PersonHit final
    {
        std::int64_t TmdbId{};
        std::wstring Name;
        std::optional<std::wstring> Photo;
    };

    struct FindResult final
    {
        std::vector<TitleHit> Movies;
        std::vector<TitleHit> Series;
        std::vector<PersonHit> People;
    };

    // `/3/search/person`: at most MaximumSearchPeople, in TMDB's order.
    [[nodiscard]] std::vector<PersonSummary> ParsePeopleSearch(winrt::Windows::Data::Json::IJsonValue const& body);
    // `/3/person/{id}?append_to_response=combined_credits`: empty when it has
    // no usable id or name (read as no such person).
    [[nodiscard]] std::optional<PersonDetails> ParsePersonDetails(winrt::Windows::Data::Json::IJsonValue const& body);
    // A person's titles from `combined_credits`: cast, directing (crew job
    // "Director") and writing (crew department "Writing"), one entry per title
    // with its roles merged. Only films and series; news, reality and talk
    // shows and appearances as oneself are dropped. Most popular first.
    [[nodiscard]] std::vector<CreditCandidate> SelectCredits(winrt::Windows::Data::Json::IJsonValue const& combinedCredits);
    // `/3/find/{id}`.
    [[nodiscard]] FindResult ParseFind(winrt::Windows::Data::Json::IJsonValue const& body);
    // `/3/{movie|tv}/{id}/external_ids`: the IMDb title id, when TMDB has one.
    [[nodiscard]] std::optional<std::wstring> ParseImdbId(winrt::Windows::Data::Json::IJsonValue const& body);

    // A long text cut to at most `max` characters, ending on its last full
    // sentence when one ends in the second half of what fits; otherwise cut
    // at `max` as it is.
    [[nodiscard]] std::wstring EndAtSentence(std::wstring const& text, std::size_t max);

    // A misspelled name. TMDB's people search forgives no typo, so a name it
    // finds nobody for is searched again one word at a time (these words,
    // longest first, at most three, none shorter than three letters; none at
    // all for a one-word query), and only people whose whole name is nearly
    // what was typed are kept (RankClosestPeople).
    [[nodiscard]] std::vector<std::wstring> ClosestPeopleWords(std::wstring_view query);
    // How many letters may differ for a name of this length to still be the one meant.
    [[nodiscard]] std::size_t AllowedDistance(std::size_t length) noexcept;
    // The edit distance between two texts, or `limit + 1` as soon as it is over `limit`.
    [[nodiscard]] std::size_t EditDistance(std::wstring_view a, std::wstring_view b, std::size_t limit);

    struct ClosestPeople final
    {
        // Nearest first, then most popular; at most three, never empty.
        std::vector<PersonSummary> People;
        // The nearest person's name, as TMDB writes it.
        std::wstring Corrected;
    };

    // The people `query` most likely means among each word's answers, or
    // empty when nobody is near enough.
    [[nodiscard]] std::optional<ClosestPeople> RankClosestPeople(
        std::wstring_view query,
        std::vector<std::vector<PersonSummary>> const& answers);

    // The Halo server's answers, built from the above.
    [[nodiscard]] winrt::Windows::Data::Json::JsonObject PeopleSearchAnswer(
        std::vector<PersonSummary> const& people,
        std::optional<std::wstring> const& corrected);

    struct MappedCredit final
    {
        CreditCandidate Credit;
        std::wstring ImdbId;
    };

    [[nodiscard]] winrt::Windows::Data::Json::JsonObject PersonAnswer(
        PersonDetails const& person,
        std::vector<MappedCredit> const& credits,
        bool partial);
    [[nodiscard]] winrt::Windows::Data::Json::JsonObject PersonLookupAnswer(PersonHit const& person);
    [[nodiscard]] winrt::Windows::Data::Json::JsonObject TitleLookupAnswer(TitleHit const& title, std::wstring const& imdbId);
}
