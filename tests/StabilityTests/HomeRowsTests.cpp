#include "HomeRowsTests.h"

#include "Controls/ShelfScrollPolicy.h"
#include "Services/CatalogLanguagePolicy.h"
#include "Services/CatalogPagingPolicy.h"
#include "Services/HomeRowsPolicy.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace
{
    void Require(bool condition, char const* message)
    {
        if (!condition)
        {
            throw std::runtime_error{ message };
        }
    }

    using HaloDesktop::ViewModels::HomeFilter;

    std::wstring Title(std::optional<std::wstring_view> name, std::wstring_view type, std::wstring_view addon)
    {
        return HaloDesktop::Services::CatalogShelfLabels(name, type, addon).Title;
    }

    void TestHomeCatalogTypes()
    {
        using HaloDesktop::Services::IsHomeCatalogType;
        Require(IsHomeCatalogType(L"movie") && IsHomeCatalogType(L"series"), "films or series were kept off Home");
        // TorBox publishes the account's torrent list as `other`.
        Require(!IsHomeCatalogType(L"other") && !IsHomeCatalogType(L"channel") && !IsHomeCatalogType(L"tv"),
            "a non-film, non-series catalog reached Home");
    }

    void TestCatalogShelfLabels()
    {
        using HaloDesktop::Services::CatalogShelfLabels;

        auto const popular = CatalogShelfLabels(L"Popular", L"movie", L"Cinemeta");
        Require(popular.Title == L"Popular Movies" && popular.Source == L"CINEMETA",
            "a catalog shelf was not titled by name and kind with its addon beside it");
        auto const featured = CatalogShelfLabels(L"Featured", L"series", L" Cinemeta ");
        Require(featured.Title == L"Featured Series" && featured.Source == L"CINEMETA",
            "a padded addon name was not trimmed into the source line");

        // A name that already says its kind is not doubled.
        Require(Title(L"Trending Movies", L"movie", L"TMDB") == L"Trending Movies", "Trending Movies was doubled");
        Require(Title(L"Top Films", L"movie", L"TMDB") == L"Top Films", "Top Films was doubled");
        Require(Title(L"Popular TV Shows", L"series", L"TMDB") == L"Popular TV Shows", "Popular TV Shows was doubled");
        Require(Title(L"Anime Series", L"series", L"Kitsu") == L"Anime Series", "Anime Series was doubled");

        // "Showcase" holds "show" but does not say the catalog is of shows.
        Require(Title(L"Showcase", L"series", L"Addon") == L"Showcase Series", "a word containing a kind word kept no kind");

        // No name is titled by its kind.
        Require(Title(std::nullopt, L"movie", L"Addon") == L"Movies", "an unnamed film catalog was not titled Movies");
        Require(Title(L"   ", L"series", L"Addon") == L"Series", "a blank series catalog was not titled Series");

        // An addon's own prefix moves from the title to the source line.
        auto const latest = CatalogShelfLabels(L"TMDB - Latest Releases", L"movie", L"The Movie Database Addon");
        Require(latest.Title == L"Latest Movie Releases" && latest.Source == L"TMDB",
            "an acronym prefix did not move to the source line");
        auto const colon = CatalogShelfLabels(L"TMDB: Popular", L"series", L"The Movie Database Addon");
        Require(colon.Title == L"Popular Series" && colon.Source == L"TMDB", "a colon prefix did not move to the source line");
        auto const own = CatalogShelfLabels(L"Cinemeta - Popular", L"movie", L"Cinemeta");
        Require(own.Title == L"Popular Movies" && own.Source == L"CINEMETA", "the addon's own name stayed in the title");

        // A prefix that is not the addon stays in the title.
        Require(Title(L"Star Wars - Saga", L"movie", L"Collections") == L"Star Wars - Saga Movies",
            "a prefix that is part of the title was removed");
        // No spaces around the hyphen: part of a word, not a separator.
        Require(Title(L"Spider-Man Picks", L"movie", L"Fan Lists") == L"Spider-Man Movie Picks",
            "a hyphenated word was split as a prefix");

        // The source line drops a trailing "Addon".
        Require(CatalogShelfLabels(L"Popular", L"movie", L"The Movie Database Addon").Source == L"THE MOVIE DATABASE",
            "the source line kept a trailing Addon");
        Require(CatalogShelfLabels(L"Popular", L"movie", L"Streaming Catalogs Add-on").Source == L"STREAMING CATALOGS",
            "the source line kept a trailing Add-on");

        // A name ending on a plural noun takes the kind in front of it.
        Require(Title(L"Latest Releases", L"movie", L"Addon") == L"Latest Movie Releases", "Latest Releases lost its kind");
        Require(Title(L"New Arrivals", L"series", L"Addon") == L"New Series Arrivals", "New Arrivals lost its kind");
        // A single word is the whole name; the kind goes behind it as usual.
        Require(Title(L"Releases", L"movie", L"Addon") == L"Releases Movies", "a one-word name took the kind in front");

        // The en and em dashes and the bar separate as the hyphen does.
        Require(Title(L"TMDB \u2013 Top Rated", L"movie", L"Addon") == L"Top Rated Movies", "an en dash prefix stayed");
        Require(Title(L"TMDB \u2014 Top Rated", L"movie", L"Addon") == L"Top Rated Movies", "an em dash prefix stayed");
        Require(Title(L"TMDB | Top Rated", L"movie", L"Addon") == L"Top Rated Movies", "a bar prefix stayed");
    }

    HaloDesktop::Services::RecentlyWatchedRow Row(
        std::wstring itemId,
        std::int64_t updatedAt,
        std::optional<std::wstring> name = std::wstring{ L"Title" },
        std::optional<std::wstring> poster = std::nullopt)
    {
        return { std::move(itemId), std::move(name), std::move(poster), updatedAt };
    }

    std::vector<std::wstring> Ids(std::vector<HaloDesktop::Services::RecentlyWatchedTitle> const& titles)
    {
        std::vector<std::wstring> ids;
        for (auto const& title : titles) ids.push_back(title.MetaId);
        return ids;
    }

    void TestRecentlyWatchedRow()
    {
        using HaloDesktop::Services::BuildRecentlyWatchedRow;
        using HaloDesktop::Services::RecentlyWatchedLibraryEntry;
        using HaloDesktop::Services::RecentlyWatchedTitle;

        // Distinct titles newest first, at any progress.
        auto const shelf = BuildRecentlyWatchedRow(
            { Row(L"series:a", 10, L"Show"), Row(L"series:a", 30, L"Show"), Row(L"movie:b", 20, L"Finished") },
            {}, {}, HomeFilter::All);
        Require(shelf.size() == 2
                && shelf[0] == RecentlyWatchedTitle{ L"series", L"a", L"Show", std::nullopt }
                && shelf[1] == RecentlyWatchedTitle{ L"movie", L"b", L"Finished", std::nullopt },
            "recently watched did not list distinct titles newest first");

        // What continue watching offers is left out, and the filter applies.
        std::vector<HaloDesktop::Services::RecentlyWatchedRow> const states{
            Row(L"series:a", 30), Row(L"movie:b", 20), Row(L"anime:kitsu:1", 10) };
        Require(Ids(BuildRecentlyWatchedRow(states, {}, { L"series:a" }, HomeFilter::All))
                == std::vector<std::wstring>({ L"b", L"kitsu:1" }),
            "recently watched repeated a continue title");
        // Anything that is not a series counts as a movie.
        Require(Ids(BuildRecentlyWatchedRow(states, {}, {}, HomeFilter::Movies))
                == std::vector<std::wstring>({ L"b", L"kitsu:1" }),
            "the Movies filter dropped a non-series title");
        Require(Ids(BuildRecentlyWatchedRow(states, {}, {}, HomeFilter::Series)) == std::vector<std::wstring>({ L"a" }),
            "the Series filter kept a film");

        // Names come from the library when a row has none; unnamed rows drop.
        std::vector<RecentlyWatchedLibraryEntry> const library{
            { L"movie:b", L"From library", std::wstring{ L"https://p.test/b.jpg" }, false } };
        auto const named = BuildRecentlyWatchedRow(
            { Row(L"movie:b", 2, std::nullopt), Row(L"movie:c", 1, std::nullopt) }, library, {}, HomeFilter::All);
        Require(named.size() == 1
                && named[0] == RecentlyWatchedTitle{ L"movie", L"b", L"From library", std::wstring{ L"https://p.test/b.jpg" } },
            "recently watched did not take a missing name and poster from the library");

        // A removed library entry names nothing.
        std::vector<RecentlyWatchedLibraryEntry> const removed{ { L"movie:b", L"Gone", std::nullopt, true } };
        Require(BuildRecentlyWatchedRow({ Row(L"movie:b", 2, std::nullopt) }, removed, {}, HomeFilter::All).empty(),
            "a removed library entry named a recently watched title");

        // Stops at fifteen.
        std::vector<HaloDesktop::Services::RecentlyWatchedRow> many;
        for (int index = 0; index < 20; ++index)
        {
            many.push_back(Row(L"movie:t" + std::to_wstring(index), index, L"Title " + std::to_wstring(index)));
        }
        auto const capped = BuildRecentlyWatchedRow(many, {}, {}, HomeFilter::All);
        Require(capped.size() == 15 && capped.front().Name == L"Title 19", "recently watched did not stop at fifteen");
    }

    void TestCatalogLanguage()
    {
        using HaloDesktop::Services::CatalogExtraDeclaration;
        using HaloDesktop::Services::CatalogLanguageGenre;
        using HaloDesktop::Services::LanguageChoices;

        std::vector<std::wstring> const languages{
            L"English", L"Hindi", L"Tamil", L"Telugu", L"French", L"German", L"Japanese", L"Korean" };
        std::vector<CatalogExtraDeclaration> const tmdbLanguage{ { L"genre", false, languages } };
        std::vector<CatalogExtraDeclaration> const cinemetaTop{
            { L"genre", false, { L"Action", L"Drama", L"Comedy", L"Romance" } }, { L"skip", false, {} } };
        std::vector<CatalogExtraDeclaration> const tmdbYear{ { L"genre", false, { L"2026", L"2025", L"2024" } } };
        std::vector<CatalogExtraDeclaration> const required{ { L"genre", true, languages } };

        auto const choices = LanguageChoices(tmdbLanguage);
        Require(choices && *choices == languages, "a catalog whose genre choices are languages did not offer them");
        Require(!LanguageChoices(cinemetaTop), "a genre list was taken for languages");
        Require(!LanguageChoices(tmdbYear), "a year list was taken for languages");
        Require(!LanguageChoices({}), "a catalog with no extras offered languages");
        Require(!LanguageChoices(required), "a required genre was offered as a language choice");

        Require(CatalogLanguageGenre(tmdbLanguage, L"Tamil") == std::optional<std::wstring>{ L"Tamil" },
            "the account language did not narrow a language catalog that offers it");
        Require(!CatalogLanguageGenre(tmdbLanguage, L"Klingon"), "a language the catalog lacks was sent");
        Require(!CatalogLanguageGenre(tmdbLanguage, std::nullopt), "a language was sent with none chosen");
        Require(!CatalogLanguageGenre(cinemetaTop, L"Hindi"), "a language was sent to a genre catalog");
    }

    HaloDesktop::Services::PagedTitle Meta(std::wstring id, std::wstring type = L"movie")
    {
        return { std::move(type), std::move(id) };
    }

    void TestCatalogPaging()
    {
        using HaloDesktop::Services::CatalogPager;
        using HaloDesktop::Services::HasMorePages;
        using HaloDesktop::Services::NewTitles;
        using HaloDesktop::Services::PagedTitle;

        // A page adds only unseen titles the grid keeps, in the addon order.
        std::vector<PagedTitle> const shown{ Meta(L"a"), Meta(L"b") };
        std::vector<PagedTitle> const page{ Meta(L"b"), Meta(L"c"), Meta(L"d", L"series"), Meta(L"e"), Meta(L"c") };
        auto const added = NewTitles(shown, page, [](PagedTitle const& title) { return title.Type == L"movie"; });
        Require(added == std::vector<std::size_t>({ 1, 3 }), "a page added a seen, repeated or filtered title");

        // The same id under another type is a different title.
        std::vector<PagedTitle> const one{ Meta(L"a") };
        std::vector<PagedTitle> const other{ Meta(L"a", L"series") };
        Require(NewTitles(one, other, {}).size() == 1, "the same id under another type was taken for the same title");

        // Paging stops at an empty page or one the addon already sent.
        std::vector<PagedTitle> const received{ Meta(L"a"), Meta(L"b") };
        Require(!HasMorePages(received, {}), "an empty page did not end the paging");
        // An addon that ignores skip answers with its first page again.
        Require(!HasMorePages(received, received), "a repeated first page did not end the paging");
        std::vector<PagedTitle> const fresh{ Meta(L"b"), Meta(L"c") };
        Require(HasMorePages(received, fresh), "a page with a new title ended the paging");

        // The pager's offset is the addon's count, filtered or not.
        std::vector<PagedTitle> const first{ Meta(L"a"), Meta(L"s", L"series"), Meta(L"b") };
        CatalogPager pager{ first, HomeFilter::Movies };
        Require(pager.NextSkip() == 3 && pager.HasMore(), "the pager did not start from the first page's length");
        std::vector<PagedTitle> const second{ Meta(L"b"), Meta(L"c"), Meta(L"t", L"series"), Meta(L"d") };
        Require(pager.Accept(second) == std::vector<std::size_t>({ 1, 3 }), "the pager added a seen or filtered title");
        Require(pager.NextSkip() == 7 && pager.HasMore(), "the pager did not advance by the page's length");
        Require(pager.Accept(second).empty() && !pager.HasMore(), "a repeated page did not end the paging");
    }

    void TestShelfScrolling()
    {
        using HaloDesktop::Controls::MeasureShelfEdges;
        using HaloDesktop::Controls::ShelfEdges;
        using HaloDesktop::Controls::ShelfPageTarget;

        Require(MeasureShelfEdges(0.0, 500.0, 500.0) == ShelfEdges{ true, true }, "a strip that fits was not at both ends");
        Require(MeasureShelfEdges(0.5, 500.0, 1000.0) == ShelfEdges{ true, false }, "the start ignored its slack");
        Require(MeasureShelfEdges(499.5, 500.0, 1000.0) == ShelfEdges{ false, true }, "the end ignored its slack");
        Require(MeasureShelfEdges(200.0, 500.0, 1000.0) == ShelfEdges{ false, false }, "a mid strip claimed an end");

        // Cards 132 wide with a 14 gap: a 146 step, and 700 + 14 fits four.
        Require(ShelfPageTarget(0.0, 700.0, 132.0, 14.0, 1) == 584.0, "a page right did not move by the whole cards that fit");
        Require(ShelfPageTarget(584.0, 700.0, 132.0, 14.0, -1) == 0.0, "a page left did not return to the start");
        // From mid-card the target lands on the nearest boundary.
        Require(ShelfPageTarget(80.0, 700.0, 132.0, 14.0, 1) == 730.0, "a page did not land on a card boundary");
        // A viewport narrower than a card still moves one card.
        Require(ShelfPageTarget(0.0, 100.0, 132.0, 14.0, 1) == 146.0, "a narrow strip did not move at least one card");
        Require(ShelfPageTarget(50.0, 700.0, 0.0, 0.0, 1) == 50.0, "a strip with no card width moved");
    }
}

void RunHomeRowsTests()
{
    TestHomeCatalogTypes();
    TestCatalogShelfLabels();
    TestRecentlyWatchedRow();
    TestCatalogLanguage();
    TestCatalogPaging();
    TestShelfScrolling();
}
