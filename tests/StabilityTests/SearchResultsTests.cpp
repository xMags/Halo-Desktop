#include "SearchResultsTests.h"

#include "Services/SearchHistoryPolicy.h"
#include "Services/SearchResultsPolicy.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{
    using HaloDesktop::Services::SearchTarget;
    using HaloDesktop::Services::SearchTargetState;
    using HaloDesktop::Services::SearchTargetStatus;
    using HaloDesktop::Services::SearchTitle;

    void Require(bool condition, char const* message)
    {
        if (!condition)
        {
            throw std::runtime_error{ message };
        }
    }

    SearchTarget Target(std::wstring key, std::wstring addonName, std::wstring type = L"movie")
    {
        return { key, key, std::move(addonName), std::move(type), L"search", L"Search" };
    }

    SearchTitle Meta(std::wstring id, std::wstring type = L"movie")
    {
        SearchTitle title;
        title.Name = id;
        title.Id = std::move(id);
        title.Type = std::move(type);
        return title;
    }

    SearchTitle Named(std::wstring id, std::wstring name, std::wstring type = L"movie")
    {
        SearchTitle title;
        title.Id = std::move(id);
        title.Type = std::move(type);
        title.Name = std::move(name);
        return title;
    }

    SearchTitle Dated(std::wstring id, std::wstring name, std::optional<std::wstring> releaseInfo = std::nullopt, std::wstring type = L"movie")
    {
        auto title = Named(std::move(id), std::move(name), std::move(type));
        title.ReleaseInfo = std::move(releaseInfo);
        return title;
    }

    // A result with a year and a poster that loads, as the reference's `full`.
    SearchTitle Full(std::wstring id, std::wstring name, std::function<void(SearchTitle&)> const& extra = {})
    {
        auto title = Named(id, std::move(name));
        title.ReleaseInfo = L"2016";
        title.Poster = L"https://image.tmdb.org/t/p/w500/" + id + L".jpg";
        if (extra) extra(title);
        return title;
    }

    SearchTargetState Pending() { return {}; }
    SearchTargetState Failed() { return { SearchTargetStatus::Failed, {} }; }
    SearchTargetState Done(std::vector<SearchTitle> titles) { return { SearchTargetStatus::Done, std::move(titles) }; }

    std::vector<std::wstring> Keys(std::vector<SearchTitle> const& titles)
    {
        std::vector<std::wstring> keys;
        for (auto const& title : titles) keys.push_back(HaloDesktop::Services::TitleKey(title));
        return keys;
    }

    std::vector<std::wstring> IdsOf(std::vector<SearchTitle> const& titles)
    {
        std::vector<std::wstring> ids;
        for (auto const& title : titles) ids.push_back(title.Id);
        return ids;
    }

    std::optional<std::wstring> TopId(
        std::vector<SearchTarget> const& targets,
        std::vector<SearchTargetState> const& states,
        std::optional<std::wstring_view> query = std::nullopt)
    {
        auto const progress = HaloDesktop::Services::SearchProgress(targets, states, query);
        return progress.TopMatch ? std::optional<std::wstring>{ progress.TopMatch->Id } : std::nullopt;
    }

    SearchTarget const CinemetaMovies = Target(L"c/movie", L"Cinemeta");
    SearchTarget const CinemetaSeries = Target(L"c/series", L"Cinemeta", L"series");
    SearchTarget const TmdbMovies = Target(L"t/movie", L"TMDB");

    void TestRowsInAddonOrder()
    {
        auto const progress = HaloDesktop::Services::SearchProgress(
            std::vector<SearchTarget>{ CinemetaMovies, CinemetaSeries, TmdbMovies },
            std::vector<SearchTargetState>{ Pending(), Done({ Meta(L"s1", L"series") }), Done({ Meta(L"m9") }) });
        Require(progress.Groups.size() == 2 && progress.Groups[0].Key == L"c/series" && progress.Groups[1].Key == L"t/movie",
            "rows did not appear as their catalogs answered, in addon order");
        Require(progress.Pending == 1, "the catalog still out was not counted");
    }

    void TestTopMatchWaitsForEarlierCatalogs()
    {
        std::vector<SearchTarget> const targets{ CinemetaMovies, TmdbMovies };
        Require(!TopId(targets, { Pending(), Done({ Meta(L"m9") }) }), "the top match did not wait for an earlier catalog");
        Require(TopId(targets, { Done({ Meta(L"m1") }), Pending() }) == L"m1", "the first catalog's title was not the top match");
        // An earlier catalog that failed or found nothing passes the choice on.
        Require(TopId(targets, { Failed(), Done({ Meta(L"m9") }) }) == L"m9", "a failed catalog held the top match back");
        Require(TopId(targets, { Done({}), Done({ Meta(L"m9") }) }) == L"m9", "an empty catalog held the top match back");
    }

    void TestFailedCatalogsReported()
    {
        std::vector<SearchTarget> const targets{ CinemetaMovies, TmdbMovies };
        auto const progress = HaloDesktop::Services::SearchProgress(targets, std::vector<SearchTargetState>{ Failed(), Done({}) });
        Require(progress.Failed.size() == 1 && progress.Failed[0].AddonName == L"Cinemeta", "a failed catalog was not reported");
        Require(progress.Groups.empty() && progress.Total == 2 && progress.Pending == 0, "a failed search's counts were wrong");
    }

    void TestUniqueTitles()
    {
        auto const unique = HaloDesktop::Services::UniqueTitles({ Meta(L"a"), Meta(L"a"), Meta(L"a", L"series") });
        Require(Keys(unique) == std::vector<std::wstring>{ L"movie:a", L"series:a" }, "a title listed twice was shown twice");
    }

    void TestInterleaveTakesTurns()
    {
        std::vector<std::vector<SearchTitle>> const lists{
            { Meta(L"tt1"), Meta(L"tt2"), Meta(L"tt3"), Meta(L"tt4") },
            { Meta(L"tt2"), Meta(L"tt9") },
            { Meta(L"tt5", L"series") },
        };
        Require(Keys(HaloDesktop::Services::Interleave(lists)) == std::vector<std::wstring>{
                    L"movie:tt1", L"movie:tt2", L"series:tt5", L"movie:tt9", L"movie:tt3", L"movie:tt4" },
            "interleave did not take turns or keep each title once");
    }

    void TestInterleaveExcludes()
    {
        std::vector<std::vector<SearchTitle>> const lists{ { Meta(L"tt1"), Meta(L"tt2") } };
        std::vector<SearchTitle> const exclude{ Meta(L"tt1") };
        Require(Keys(HaloDesktop::Services::Interleave(lists, exclude)) == std::vector<std::wstring>{ L"movie:tt2" },
            "interleave kept a title shown elsewhere");
    }

    void TestInterleaveMergesDifferentIds()
    {
        std::vector<std::vector<SearchTitle>> const lists{
            { Dated(L"tt1160419", L"Dune", L"2021"), Dated(L"tt15239678", L"Dune: Part Two", L"2024") },
            { Dated(L"tmdb:438631", L"Dune", L"2021"), Dated(L"tmdb:693134", L"Dune Part Two", L"2024") },
            { Dated(L"tt10466872", L"Dune: Prophecy", L"2024–", L"series"), Dated(L"tmdb:90228", L"Dune: Prophecy", L"2024-2025", L"series") },
        };
        Require(Keys(HaloDesktop::Services::Interleave(lists)) == std::vector<std::wstring>{
                    L"movie:tt1160419", L"series:tt10466872", L"movie:tt15239678" },
            "two addons' ids for one title were not merged into the better-ranked copy");
    }

    void TestInterleaveKeepsDifferentYearsKinds()
    {
        std::vector<std::vector<SearchTitle>> const lists{
            { Dated(L"tt1160419", L"Dune", L"2021"), Dated(L"tt0087182", L"Dune", L"1984") },
            { Dated(L"tt0142032", L"Dune", L"2000", L"series"), Dated(L"tmdb:1", L"Dune"), Dated(L"tmdb:2", L"Dune") },
        };
        Require(Keys(HaloDesktop::Services::Interleave(lists)) == std::vector<std::wstring>{
                    L"movie:tt1160419", L"series:tt0142032", L"movie:tt0087182", L"movie:tmdb:1", L"movie:tmdb:2" },
            "same-named titles of other years, kinds or without a year were merged");
    }

    void TestInterleaveExcludesOtherAddonsCopy()
    {
        std::vector<std::vector<SearchTitle>> const lists{ { Dated(L"tmdb:438631", L"Dune", L"2021"), Meta(L"tt9") } };
        std::vector<SearchTitle> const exclude{ Dated(L"tt1160419", L"Dune", L"2021") };
        Require(Keys(HaloDesktop::Services::Interleave(lists, exclude)) == std::vector<std::wstring>{ L"movie:tt9" },
            "another addon's copy of what is shown above was kept");
    }

    void TestByRelevance()
    {
        std::vector<SearchTitle> const merged{
            Named(L"1", L"Doctor Stranger", L"series"),
            Named(L"2", L"Doctor Strange in the Multiverse of Madness"),
            Named(L"3", L"Amuthu Dosthara", L"series"),
            Named(L"4", L"Dr. Strangelove"),
            Named(L"5", L"Doctor Strange"),
            Named(L"6", L"The Strange Case of Doctor Rx"),
        };
        // "Dr. Strangelove" reads as "doctor strangelove", which begins with the query as "Doctor Stranger" does.
        Require(IdsOf(HaloDesktop::Services::ByRelevance(merged, L"doctor strange")) == std::vector<std::wstring>{
                    L"5", L"2", L"1", L"4", L"6", L"3" },
            "names answering the query did not come first, each level keeping its order");
    }

    void TestRelevanceIgnoresCaseAccentsPunctuation()
    {
        using HaloDesktop::Services::NameRelevance;
        Require(NameRelevance(L"Amélie", L"amelie") == 0, "an accent changed the relevance");
        Require(NameRelevance(L"Spider-Man: No Way Home", L"spider man") == 1, "punctuation changed the relevance");
        Require(NameRelevance(L"WALL·E", L"wall e") == 0, "a middle dot was not read as a word break");
    }

    void TestTopMatchWithQuery()
    {
        std::vector<SearchTarget> const targets{ CinemetaMovies, CinemetaSeries, TmdbMovies };
        auto const film = Named(L"m1", L"El Camino: A Breaking Bad Movie");
        auto const show = Named(L"s1", L"Breaking Bad", L"series");
        auto const other = Named(L"m9", L"Breaking Bad Habits");
        std::wstring_view const query{ L"breaking bad" };
        // All answered: the series named exactly what was typed wins over the film asked first.
        Require(TopId(targets, { Done({ film }), Done({ show }), Done({ other }) }, query) == L"s1",
            "the exact name did not win once every catalog answered");
        // Still waiting on a later catalog: a title named exactly what was typed is shown already...
        Require(TopId(targets, { Done({ film }), Done({ show }), Pending() }, query) == L"s1",
            "an exact name waited for a later catalog");
        // ...but one that only begins with it waits...
        Require(!TopId(targets, { Done({ other }), Pending(), Pending() }, query), "a name that only begins with the query was shown early");
        Require(TopId(targets, { Done({ other }), Done({ film }), Done({ show }) }, query) == L"s1",
            "a later exact name did not win once every catalog answered");
        // ...and nothing is shown while an earlier catalog is out.
        Require(!TopId(targets, { Pending(), Done({ show }), Done({ other }) }, query), "a top match was shown with an earlier catalog out");
        Require(!TopId(targets, { Done({ film }), Pending(), Done({ other }) }, query), "a top match was shown with a catalog out");
        // A name that merely holds the words waits for everyone.
        Require(!TopId(targets, { Done({ film }), Pending(), Pending() }, query), "a loose name was shown early");
        // A tie goes to the earlier catalog.
        Require(TopId(targets, { Done({ other }), Done({ film }), Done({ film }) }, L"nothing alike") == L"m9",
            "a tie did not go to the earlier catalog");
    }

    void TestRefineDropsUnrelated()
    {
        std::vector<SearchTitle> const results{
            Full(L"tmdb:1", L"Doctor Strange"), Full(L"tmdb:2", L"Faust and the Devil"), Full(L"tmdb:3", L"Dr. Strange") };
        Require(IdsOf(HaloDesktop::Services::RefineResults(results, L"doctor strange")) == std::vector<std::wstring>{ L"tmdb:1", L"tmdb:3" },
            "a result sharing no word with the query stayed");
        // An addon answering in another language names nothing: all of it stays.
        std::vector<SearchTitle> const localized{
            Full(L"tmdb:1", L"डॉक्टर स्ट्रेंज"),
            Full(L"tmdb:2", L"कुछ और") };
        Require(HaloDesktop::Services::RefineResults(localized, L"doctor strange").size() == 2,
            "an answer in another language was emptied");
    }

    void TestRefineYearAndArtwork()
    {
        std::wstring const noPoster = L"https://image.tmdb.org/t/p/w500null";
        std::vector<SearchTitle> const results{
            Full(L"tmdb:1", L"Doctor Strange Love", [&noPoster](SearchTitle& t) { t.Poster = noPoster; }),
            Full(L"tmdb:2", L"Untitled Doctor Strange Movie", [&noPoster](SearchTitle& t) { t.Poster = noPoster; t.ReleaseInfo = L""; }),
            Full(L"tt3", L"Doctor Strange 3", [](SearchTitle& t) { t.ReleaseInfo.reset(); t.Poster = L"https://images.metahub.space/poster/small/tt3/img"; }),
            Full(L"tt4", L"Doctor Strange: Multiversal Madness", [](SearchTitle& t) { t.Poster = L"https://images.metahub.space/poster/small/tt4/img"; }),
            Full(L"tmdb:5", L"Doctor Strange in the Multiverse of Madness"),
            Full(L"tmdb:6", L"The Strange Case of Doctor Rx"),
        };
        // tt3 and tt4 have poster links, and the links are found to be dead.
        std::unordered_set<std::wstring> const dead{ L"movie:tt3", L"movie:tt4" };
        Require(IdsOf(HaloDesktop::Services::RefineResults(results, L"doctor strange", dead)) == std::vector<std::wstring>{
                    L"tmdb:5", L"tmdb:6", L"tmdb:1", L"tt4" },
            "titles without a year or artwork were not left out or put last");
        // Before the links are known to be dead, those titles are judged on what the answer says.
        Require(IdsOf(HaloDesktop::Services::RefineResults(results, L"doctor strange")) == std::vector<std::wstring>{
                    L"tt4", L"tmdb:5", L"tmdb:6", L"tmdb:1", L"tt3" },
            "titles were judged artless before their links were known to be dead");
    }

    void TestTmdbMissingImage()
    {
        using HaloDesktop::Services::HasNoPoster;
        Require(HasNoPoster(std::optional<std::wstring>{ L"https://image.tmdb.org/t/p/w500null" }), "TMDB's missing-image link counted as a poster");
        Require(!HasNoPoster(std::optional<std::wstring>{ L"https://image.tmdb.org/t/p/w500/abc.jpg" }), "a TMDB poster counted as none");
        Require(HasNoPoster(std::optional<std::wstring>{}), "no poster counted as one");
    }

    void TestCopiesEnrichByImdbId()
    {
        SearchTitle cinemeta = Dated(L"tt0035767", L"The Crime Doctor's Strangest Case", L"1943");
        cinemeta.Poster = L"https://images.metahub.space/poster/small/tt0035767/img";
        SearchTitle tmdb = Dated(L"tmdb:84650", L"Crime Doctor: Strangest Case", L"1944");
        tmdb.Rating = L"6.3";
        tmdb.Poster = L"https://image.tmdb.org/t/p/w500/4at.jpg";
        tmdb.ImdbId = L"tt0035767";
        std::vector<std::vector<SearchTitle>> const lists{ { cinemeta }, { tmdb } };
        auto const merged = HaloDesktop::Services::Interleave(lists);
        Require(merged.size() == 1 && merged[0].Id == L"tt0035767", "two addons' copies were not one, by IMDb id");
        Require(merged[0].Rating == L"6.3", "the kept copy did not gain the other's rating");
        Require(merged[0].Poster == L"https://image.tmdb.org/t/p/w500/4at.jpg", "the kept copy did not gain the poster that loads");
    }

    void TestScreenfulDropsArtless()
    {
        std::vector<SearchTitle> complete;
        for (int index = 0; index < 8; ++index)
        {
            complete.push_back(Full(L"tmdb:" + std::to_wstring(index), L"Doctor Strange " + std::to_wstring(index)));
        }
        auto const artless = Full(L"tmdb:99", L"Doctor Strange Love", [](SearchTitle& t) { t.Poster = L"https://image.tmdb.org/t/p/w500null"; });
        auto all = complete;
        all.push_back(artless);
        auto const refined = HaloDesktop::Services::RefineResults(all, L"doctor strange");
        Require(std::none_of(refined.begin(), refined.end(), [](SearchTitle const& t) { return t.Id == L"tmdb:99"; }),
            "an artless title stayed beside a screenful of complete ones");
        // One short of a screenful: the artless one is still worth listing, last.
        std::vector<SearchTitle> fewer(complete.begin() + 1, complete.end());
        fewer.push_back(artless);
        auto const kept = HaloDesktop::Services::RefineResults(fewer, L"doctor strange");
        Require(!kept.empty() && kept.back().Id == L"tmdb:99", "an artless title was dropped short of a screenful");
    }

    void TestShortForms()
    {
        using HaloDesktop::Services::NameRelevance;
        Require(NameRelevance(L"Dr. Strange", L"doctor strange") == 0, "Dr. was not Doctor");
        Require(NameRelevance(L"Doctor Strange", L"dr strange") == 0, "dr was not doctor");
        Require(NameRelevance(L"Mr. Robot", L"mister robot") == 0, "Mr. was not Mister");
    }

    void TestScreenfulDropsLooseNames()
    {
        std::vector<SearchTitle> named;
        for (int index = 0; index < 8; ++index)
        {
            named.push_back(Full(L"tmdb:" + std::to_wstring(index), L"Doctor Strange " + std::to_wstring(index)));
        }
        std::vector<SearchTitle> const loose{ Full(L"tmdb:90", L"Doctor Sleep"), Full(L"tmdb:91", L"Strangers") };
        auto all = named;
        all.insert(all.end(), loose.begin(), loose.end());
        Require(IdsOf(HaloDesktop::Services::RefineResults(all, L"doctor strange")) == IdsOf(named),
            "titles naming only some words stayed beside a screenful naming all");
        std::vector<SearchTitle> fewer(named.begin() + 1, named.end());
        fewer.insert(fewer.end(), loose.begin(), loose.end());
        Require(HaloDesktop::Services::RefineResults(fewer, L"doctor strange").size() == 9,
            "loose names were dropped short of a screenful");
    }

    void TestFileNameEntry()
    {
        SearchTitle fromFile = Named(L"mf1706190", L"Doctor Strange (2016)");
        fromFile.Poster = L"https://mediafusion.test/poster/movie/mf1706190.jpg";
        SearchTitle real = Dated(L"tt1211837", L"Doctor Strange", L"2016");
        real.Poster = L"https://live.metahub.space/poster/medium/tt1211837/img";
        // Whichever is ranked first, one card remains, under the IMDb id and without the drawn poster.
        for (auto const& lists : { std::vector<std::vector<SearchTitle>>{ { fromFile }, { real } },
                                   std::vector<std::vector<SearchTitle>>{ { real }, { fromFile } } })
        {
            auto const merged = HaloDesktop::Services::Interleave(lists);
            Require(merged.size() == 1, "a file-name entry was not folded into the title it names");
            Require(merged[0].Id == L"tt1211837" && merged[0].Name == L"Doctor Strange", "the IMDb copy did not lead");
            Require(merged[0].Poster == real.Poster, "the drawn poster replaced the real one");
        }
        // With no real copy to fold into, it keeps its place with the year out of its name.
        std::vector<std::vector<SearchTitle>> const alone{ { fromFile } };
        auto const single = HaloDesktop::Services::Interleave(alone);
        Require(single.size() == 1 && single[0].Name == L"Doctor Strange" && single[0].ReleaseInfo == L"2016",
            "a file-name entry kept its year in its name");
    }

    void TestDrawnPosterIsNoPoster()
    {
        using HaloDesktop::Services::HasNoPoster;
        auto const poster = [](wchar_t const* url) { return std::optional<std::wstring>{ url }; };
        Require(HasNoPoster(poster(L"https://mediafusion.test/poster/movie/mf1706190.jpg")), "a drawn poster counted as artwork");
        Require(HasNoPoster(poster(L"https://mediafusion.test/poster/series/mf12.png")), "a drawn series poster counted as artwork");
        Require(HasNoPoster(poster(L"https://mediafusion.test/poster/movie/mf12.jpg?v=3")), "a drawn poster with a query counted as artwork");
        // Known by its id too, wherever the addon serves the card from.
        Require(HasNoPoster(poster(L"https://cdn.test/cards/12.jpg"), std::wstring_view{ L"mf12" }), "a file-name entry's card counted as artwork");
        Require(!HasNoPoster(poster(L"https://cdn.test/cards/12.jpg"), std::wstring_view{ L"tt12" }), "an IMDb title's card counted as none");
        Require(!HasNoPoster(poster(L"https://live.metahub.space/poster/medium/tt1211837/img")), "a metahub poster counted as none");
    }

    void TestProductCodes()
    {
        std::vector<SearchTitle> const results{
            Full(L"tmdb:1", L"Doctor Strange"),
            Full(L"tmdb:2", L"MIMK-180 I received a strange treatment from a doctor"),
            Full(L"tmdb:3", L"THX 1138"),
            Full(L"tmdb:4", L"WALL-E"),
        };
        Require(IdsOf(HaloDesktop::Services::RefineResults(results, L"e")) == std::vector<std::wstring>{ L"tmdb:4", L"tmdb:1" },
            "a product-code title stayed, or the rest were misordered");
        Require(IdsOf(HaloDesktop::Services::RefineResults(results, L"strange doctor")) == std::vector<std::wstring>{ L"tmdb:1" },
            "a product-code title matching every word stayed");
    }

    void TestLibraryMatches()
    {
        using HaloDesktop::Services::SearchLibraryEntry;
        std::vector<SearchLibraryEntry> const library{
            { L"movie:tt1", L"movie", L"Dune", std::nullopt, 1, false },
            { L"movie:tt2", L"movie", L"Dune: Part Two", L"p.jpg", 5, false },
            { L"movie:tt3", L"movie", L"Amélie", std::nullopt, 3, false },
            { L"series:tt4", L"series", L"Dune: Prophecy", std::nullopt, 4, true },
        };
        auto const dune = HaloDesktop::Services::LibraryMatches(library, L"dune");
        Require(dune.size() == 2, "library matches were not every live entry naming the query");
        Require(dune[0].Id == L"tt2" && dune[0].Type == L"movie" && dune[0].Name == L"Dune: Part Two" && dune[0].Poster == L"p.jpg",
            "the newest library match was not first, as a title with its poster");
        Require(dune[1].Id == L"tt1" && dune[1].Name == L"Dune" && !dune[1].Poster, "an entry without a poster gained one");
        Require(Keys(HaloDesktop::Services::LibraryMatches(library, L"TWO dune")) == std::vector<std::wstring>{ L"movie:tt2" },
            "library matching did not need every word, ignoring case");
        Require(Keys(HaloDesktop::Services::LibraryMatches(library, L"amelie")) == std::vector<std::wstring>{ L"movie:tt3" },
            "library matching did not ignore accents");
        Require(HaloDesktop::Services::LibraryMatches(library, L"   ").empty(), "a blank query matched the library");
    }

    void TestSuggestedSearches()
    {
        std::vector<std::wstring> const history{ L"dune part two", L"Dune", L"the office" };
        using HaloDesktop::Services::SuggestedSearches;
        Require(SuggestedSearches(history, L"dun") == std::vector<std::wstring>{ L"dune part two", L"Dune" },
            "past searches holding what is typed were not offered");
        // What is already typed is not offered back.
        Require(SuggestedSearches(history, L"dune") == std::vector<std::wstring>{ L"dune part two" },
            "the typed term was offered back");
        Require(SuggestedSearches(history, L"").empty(), "an empty box offered suggestions");
        std::vector<std::wstring> const many{ L"a1x", L"a2x", L"a3x", L"a4x", L"a5x", L"a6x" };
        Require(SuggestedSearches(many, L"x").size() == 5, "more than five suggestions were offered");
    }

    void TestNotesAndQueryLimits()
    {
        using HaloDesktop::Services::SearchFailureNote;
        std::vector<SearchTarget> const all{ CinemetaMovies, CinemetaSeries };
        Require(SearchFailureNote(all, 2) == L"Search could not reach your addons. Check your connection and try again.",
            "every catalog failing was not reported as such");
        Require(SearchFailureNote(all, 3) == L"Cinemeta did not answer, so its results are missing.",
            "one addon's failed catalogs were not named once");
        std::vector<SearchTarget> const two{ CinemetaMovies, TmdbMovies };
        Require(SearchFailureNote(two, 4) == L"Cinemeta, TMDB did not answer, so their results are missing.",
            "two addons that did not answer were not named");
        Require(SearchFailureNote({}, 4).empty(), "a note was written with no failure");
        Require(HaloDesktop::Services::SearchStillAskingNote(1) == L"Still asking 1 more catalog…", "the one-catalog note was wrong");
        Require(HaloDesktop::Services::SearchStillAskingNote(3) == L"Still asking 3 more catalogs…", "the catalogs note was wrong");

        Require(HaloDesktop::Services::CappedQuery(std::wstring(130, L'a')).size() == 120, "the query was not capped at 120");
        std::wstring split(119, L'a');
        split.append(L"\U0001F600");
        Require(HaloDesktop::Services::CappedQuery(split).size() == 119, "the cap split a surrogate pair");
        Require(!HaloDesktop::Services::IsSearchableQuery(L" a "), "a one-letter query searched");
        Require(HaloDesktop::Services::IsSearchableQuery(L" ab "), "a two-letter query did not search");
        Require(HaloDesktop::Services::TrimmedText(L"  dune 　") == L"dune", "trimming did not strip Unicode spaces");
    }

    void TestFolding()
    {
        using HaloDesktop::Services::FoldedText;
        Require(FoldedText(L"Amélie") == L"amelie", "a precomposed accent was not folded");
        Require(FoldedText(L"Amélie") == L"amelie", "a combining accent was not folded");
        Require(FoldedText(L"ÅNGSTRÖM") == L"angstrom", "upper-case accents were not folded");
        Require(FoldedText(L"Łódź") == L"łodz", "a letter with no decomposition was changed");
        Require(FoldedText(L"Pokémon ÉÈ") == L"pokemon ee", "mixed accents were not folded");
    }

    void TestSearchHistory()
    {
        using HaloDesktop::Services::AddSearchTerm;
        using HaloDesktop::Services::RemoveSearchTerm;
        using HaloDesktop::Services::SearchHistoryEntry;
        std::vector<SearchHistoryEntry> history{ { L"Dune", 5 }, { L"the office", 4 } };
        auto added = AddSearchTerm(history, L"  dune  ", 9);
        Require(added == std::vector<SearchHistoryEntry>{ { L"dune", 9 }, { L"the office", 4 } },
            "a repeat in another casing was not moved to the front with the newest casing");
        Require(AddSearchTerm(history, L" d ", 9) == history, "a term shorter than two letters was kept");
        std::vector<SearchHistoryEntry> full;
        for (int index = 0; index < 20; ++index) full.push_back({ L"term " + std::to_wstring(index), index });
        auto const capped = AddSearchTerm(full, L"newest", 99);
        Require(capped.size() == 20 && capped.front().Term == L"newest" && capped.back().Term == L"term 18",
            "the history was not kept to twenty, newest first");
        Require(RemoveSearchTerm(added, L"DUNE") == added, "removal was not by the exact term");
        Require(RemoveSearchTerm(added, L"dune") == std::vector<SearchHistoryEntry>{ { L"the office", 4 } }, "a term was not removed");
    }
}

void RunSearchResultsTests()
{
    TestRowsInAddonOrder();
    TestTopMatchWaitsForEarlierCatalogs();
    TestFailedCatalogsReported();
    TestUniqueTitles();
    TestInterleaveTakesTurns();
    TestInterleaveExcludes();
    TestInterleaveMergesDifferentIds();
    TestInterleaveKeepsDifferentYearsKinds();
    TestInterleaveExcludesOtherAddonsCopy();
    TestByRelevance();
    TestRelevanceIgnoresCaseAccentsPunctuation();
    TestTopMatchWithQuery();
    TestRefineDropsUnrelated();
    TestRefineYearAndArtwork();
    TestTmdbMissingImage();
    TestCopiesEnrichByImdbId();
    TestScreenfulDropsArtless();
    TestShortForms();
    TestScreenfulDropsLooseNames();
    TestFileNameEntry();
    TestDrawnPosterIsNoPoster();
    TestProductCodes();
    TestLibraryMatches();
    TestSuggestedSearches();
    TestNotesAndQueryLimits();
    TestFolding();
    TestSearchHistory();
}
