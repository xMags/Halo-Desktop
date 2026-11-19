#include "SearchResultsTests.h"

#include "Services/SearchHistoryPolicy.h"
#include "Services/SearchRankingPolicy.h"
#include "Services/SearchResultBoard.h"
#include "Services/SearchResultsPolicy.h"
#include "Services/SearchRevealPolicy.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using HaloDesktop::Services::RankedTitle;
    using HaloDesktop::Services::SearchAnswer;
    using HaloDesktop::Services::SearchQueryParts;
    using HaloDesktop::Services::SearchResultBoard;
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

    std::vector<std::wstring> SortedIds(std::vector<SearchTitle> const& titles)
    {
        auto ids = IdsOf(titles);
        std::sort(ids.begin(), ids.end());
        return ids;
    }

    SearchTarget const CinemetaMovies = Target(L"c/movie", L"Cinemeta");
    SearchTarget const CinemetaSeries = Target(L"c/series", L"Cinemeta", L"series");
    SearchTarget const TmdbMovies = Target(L"t/movie", L"TMDB");

    // A fixed year, so the tests do not depend on the clock.
    SearchQueryParts Parts(std::wstring_view query)
    {
        return HaloDesktop::Services::ParseSearchQuery(query, 2026);
    }

    SearchAnswer Answer(SearchTarget target, std::vector<SearchTitle> titles, std::size_t rank)
    {
        return { std::move(target), std::move(titles), rank };
    }

    std::vector<SearchTitle> Titles(std::vector<RankedTitle> const& ranked)
    {
        std::vector<SearchTitle> titles;
        for (auto const& entry : ranked) titles.push_back(entry.Title);
        return titles;
    }

    std::vector<SearchTitle> Ranked(std::vector<SearchAnswer> const& answers, std::wstring_view query)
    {
        return Titles(HaloDesktop::Services::RankForReveal(answers, Parts(query)));
    }

    // One catalog's titles as a reveal would list them: ranked, then judged
    // by the cutoffs (the top match left in).
    std::vector<SearchTitle> Refined(std::vector<SearchTitle> const& titles, std::wstring_view query)
    {
        std::vector<SearchAnswer> const answers{ Answer(TmdbMovies, titles, 0) };
        auto const ranked = HaloDesktop::Services::RankForReveal(answers, Parts(query));
        auto const cutoffs = HaloDesktop::Services::ComputeCutoffs(ranked);
        std::vector<SearchTitle> kept;
        for (auto const& entry : ranked)
        {
            if (HaloDesktop::Services::PassesCutoffs(entry, cutoffs)) kept.push_back(entry.Title);
        }
        return kept;
    }

    void TestRowsInAddonOrder()
    {
        auto const progress = HaloDesktop::Services::SearchProgress(
            std::vector<SearchTarget>{ CinemetaMovies, CinemetaSeries, TmdbMovies },
            std::vector<SearchTargetState>{ Pending(), Done({ Meta(L"s1", L"series") }), Done({ Meta(L"m9") }) });
        Require(progress.Groups.size() == 2 && progress.Groups[0].Key == L"c/series" && progress.Groups[1].Key == L"t/movie",
            "rows did not appear as their catalogs answered, in addon order");
        Require(progress.Pending == 1, "the catalog still out was not counted");
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

    std::vector<std::wstring> SortedKeys(std::vector<SearchTitle> const& titles)
    {
        auto keys = Keys(titles);
        std::sort(keys.begin(), keys.end());
        return keys;
    }

    void TestCopiesMergeAcrossIds()
    {
        std::vector<SearchAnswer> const answers{
            Answer(CinemetaMovies, { Dated(L"tt1160419", L"Dune", L"2021"), Dated(L"tt15239678", L"Dune: Part Two", L"2024") }, 0),
            Answer(TmdbMovies, { Dated(L"tmdb:438631", L"Dune", L"2021"), Dated(L"tmdb:693134", L"Dune Part Two", L"2024") }, 1),
            Answer(CinemetaSeries, { Dated(L"tt10466872", L"Dune: Prophecy", L"2024–", L"series"), Dated(L"tmdb:90228", L"Dune: Prophecy", L"2024-2025", L"series") }, 2),
        };
        Require(SortedKeys(Ranked(answers, L"dune")) == std::vector<std::wstring>{
                    L"movie:tt1160419", L"movie:tt15239678", L"series:tt10466872" },
            "two addons' ids for one title were not merged into the better-ranked copy");
    }

    void TestCopiesKeepDifferentYearsKinds()
    {
        std::vector<SearchAnswer> const answers{
            Answer(CinemetaMovies, { Dated(L"tt1160419", L"Dune", L"2021"), Dated(L"tt0087182", L"Dune", L"1984") }, 0),
            Answer(TmdbMovies, { Dated(L"tt0142032", L"Dune", L"2000", L"series"), Dated(L"tmdb:1", L"Dune"), Dated(L"tmdb:2", L"Dune") }, 1),
        };
        Require(SortedKeys(Ranked(answers, L"dune")) == std::vector<std::wstring>{
                    L"movie:tmdb:1", L"movie:tmdb:2", L"movie:tt0087182", L"movie:tt1160419", L"series:tt0142032" },
            "same-named titles of other years, kinds or without a year were merged");
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
        std::vector<SearchAnswer> const answers{ Answer(CinemetaMovies, { cinemeta }, 0), Answer(TmdbMovies, { tmdb }, 1) };
        auto const merged = Ranked(answers, L"crime doctor");
        Require(merged.size() == 1 && merged[0].Id == L"tt0035767", "two addons' copies were not one, by IMDb id");
        Require(merged[0].Rating == L"6.3", "the kept copy did not gain the other's rating");
        Require(merged[0].Poster == L"https://image.tmdb.org/t/p/w500/4at.jpg", "the kept copy did not gain the poster that loads");
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
        Require(IdsOf(Refined(all, L"doctor strange")) == IdsOf(named),
            "titles naming only some words stayed beside a screenful naming all");
        std::vector<SearchTitle> fewer(named.begin() + 1, named.end());
        fewer.insert(fewer.end(), loose.begin(), loose.end());
        Require(Refined(fewer, L"doctor strange").size() == 9,
            "loose names were dropped short of a screenful");
    }

    void TestFileNameEntry()
    {
        SearchTitle fromFile = Named(L"mf1706190", L"Doctor Strange (2016)");
        fromFile.Poster = L"https://mediafusion.test/poster/movie/mf1706190.jpg";
        SearchTitle real = Dated(L"tt1211837", L"Doctor Strange", L"2016");
        real.Poster = L"https://live.metahub.space/poster/medium/tt1211837/img";
        // Whichever is ranked first, one card remains, under the IMDb id and without the drawn poster.
        for (auto const& answers : { std::vector<SearchAnswer>{ Answer(CinemetaMovies, { fromFile }, 0), Answer(TmdbMovies, { real }, 1) },
                                     std::vector<SearchAnswer>{ Answer(CinemetaMovies, { real }, 0), Answer(TmdbMovies, { fromFile }, 1) } })
        {
            auto const merged = Ranked(answers, L"doctor strange");
            Require(merged.size() == 1, "a file-name entry was not folded into the title it names");
            Require(merged[0].Id == L"tt1211837" && merged[0].Name == L"Doctor Strange", "the IMDb copy did not lead");
            Require(merged[0].Poster == real.Poster, "the drawn poster replaced the real one");
        }
        // With no real copy to fold into, it keeps its place with the year out of its name.
        std::vector<SearchAnswer> const alone{ Answer(CinemetaMovies, { fromFile }, 0) };
        auto const single = Ranked(alone, L"doctor strange");
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
        Require(IdsOf(Refined(results, L"e")) == std::vector<std::wstring>{ L"tmdb:4", L"tmdb:1" },
            "a product-code title stayed, or the rest were misordered");
        Require(IdsOf(Refined(results, L"strange doctor")) == std::vector<std::wstring>{ L"tmdb:1" },
            "a product-code title matching every word stayed");
    }

    void TestRevealTopMatchPrefersExactName()
    {
        auto const film = Full(L"m1", L"El Camino: A Breaking Bad Movie");
        auto const show = Full(L"s1", L"Breaking Bad", [](SearchTitle& t) { t.Type = L"series"; });
        auto const other = Full(L"m9", L"Breaking Bad Habits");
        // The series named exactly what was typed wins over the film of the catalog asked first.
        std::vector<SearchAnswer> const answers{
            Answer(CinemetaMovies, { film }, 0), Answer(CinemetaSeries, { show }, 0), Answer(TmdbMovies, { other }, 1) };
        SearchResultBoard board;
        auto const revealed = board.Reveal(answers, Parts(L"breaking bad"));
        Require(revealed.TopMatch && revealed.TopMatch->Id == L"s1", "the exact name was not the top match");
        Require(IdsOf(revealed.Titles) == std::vector<std::wstring>{ L"m9", L"m1" },
            "the top match was listed again, or the rest were not ranked by name");
        // With a year typed, the title from that year is the top match.
        std::vector<SearchAnswer> const dunes{
            Answer(CinemetaMovies, { Full(L"tt0087182", L"Dune", [](SearchTitle& t) { t.ReleaseInfo = L"1984"; }),
                                     Full(L"tt1160419", L"Dune", [](SearchTitle& t) { t.ReleaseInfo = L"2021"; }) }, 0) };
        SearchResultBoard dated;
        auto const dune = dated.Reveal(dunes, Parts(L"dune 2021"));
        Require(dune.TopMatch && dune.TopMatch->Id == L"tt1160419", "the title of the typed year was not the top match");
    }

    void TestRevealTopMatchIgnoresFailedAndEmpty()
    {
        // A failed catalog gives no answer; an empty one gives no titles. Neither holds the choice back.
        std::vector<SearchAnswer> const answers{
            Answer(CinemetaMovies, {}, 0), Answer(TmdbMovies, { Full(L"tt1", L"Doctor Strange") }, 1) };
        SearchResultBoard board;
        auto const revealed = board.Reveal(answers, Parts(L"doctor strange"));
        Require(revealed.TopMatch && revealed.TopMatch->Id == L"tt1", "an empty catalog held the top match back");
        Require(revealed.Titles.empty(), "the top match was also listed");
        SearchResultBoard none;
        auto const nothing = none.Reveal({}, Parts(L"doctor strange"));
        Require(!nothing.TopMatch && nothing.Titles.empty() && none.Revealed(), "a search with no answers showed something");
    }

    void TestLooseNameIsNoTopMatch()
    {
        SearchResultBoard board;
        std::vector<SearchAnswer> const loose{ Answer(CinemetaMovies, { Full(L"m1", L"El Camino: A Breaking Bad Movie") }, 0) };
        auto const revealed = board.Reveal(loose, Parts(L"breaking bad"));
        Require(!revealed.TopMatch, "a name merely holding the words was the top match");
        Require(IdsOf(revealed.Titles) == std::vector<std::wstring>{ L"m1" }, "the loose name was not listed");
        // An exact name from another year than the one typed is not the top match either.
        SearchResultBoard dated;
        std::vector<SearchAnswer> const old{
            Answer(CinemetaMovies, { Full(L"tt0087182", L"Dune", [](SearchTitle& t) { t.ReleaseInfo = L"1984"; }) }, 0) };
        Require(!dated.Reveal(old, Parts(L"dune 2021")).TopMatch, "a title of another year than the one typed was the top match");
    }

    void TestPersonQueryKeepsTheirWork()
    {
        using HaloDesktop::Services::SearchQueryMeaning;
        std::wstring const noPoster = L"https://image.tmdb.org/t/p/w500null";
        std::vector<SearchTitle> const results{
            Full(L"tt1", L"Inside Christopher Nolan's Oppenheimer"),
            Full(L"tt2", L"Interstellar"),
            Full(L"tt3", L"Christopher Columbus"),
            Full(L"tmdb:4", L"Some Short", [&noPoster](SearchTitle& t) { t.Poster = noPoster; }),
            Full(L"tt5", L"Inception"),
        };
        std::vector<SearchAnswer> const answers{ Answer(CinemetaMovies, results, 0) };
        // As a title's name: only what is named like the query.
        SearchResultBoard plain;
        Require(IdsOf(plain.Reveal(answers, Parts(L"christopher nolan")).Titles) == std::vector<std::wstring>{ L"tt1", L"tt3" },
            "a name nobody is known by kept titles sharing no word with it");
        // As a person's name: their films stay after the titles named like
        // the query, in the addon's order; a name holding one word of it and
        // a film without artwork go.
        SearchResultBoard person;
        auto const revealed = person.Reveal(answers, Parts(L"christopher nolan"), SearchQueryMeaning::Person);
        Require(IdsOf(revealed.Titles) == std::vector<std::wstring>{ L"tt1", L"tt2", L"tt5" },
            "a person's films were not kept after the titles named like the query");
        Require(revealed.Cutoffs.KeepCredited && revealed.Cutoffs.DropSomeWords, "a person's name set the wrong cutoffs");
        // A late answer is judged the same way.
        auto const diff = person.Absorb(Answer(TmdbMovies, { Full(L"tt6", L"Tenet"), Full(L"tt7", L"Christopher Robin") }, 1));
        Require(IdsOf(diff.Appends) == std::vector<std::wstring>{ L"tt6" }, "a late answer was not judged as the person's work");
        // A title named exactly that is what was meant, person or not.
        std::vector<SearchAnswer> const film{ Answer(CinemetaMovies, { Full(L"tt8", L"Dune"), Full(L"tt9", L"Arrival") }, 0) };
        SearchResultBoard exact;
        auto const dune = exact.Reveal(film, Parts(L"dune"), SearchQueryMeaning::Person);
        Require(!dune.Cutoffs.KeepCredited && dune.Titles.empty() && dune.TopMatch && dune.TopMatch->Id == L"tt8",
            "a title named exactly the query was treated as a person's name");
    }

    void TestMissingPosterLinks()
    {
        using HaloDesktop::Services::RevealPosterLinks;
        using HaloDesktop::Services::SearchQueryMeaning;
        std::wstring const noPoster = L"https://image.tmdb.org/t/p/w500null";
        auto const poster = [](std::wstring const& id) { return L"https://images.metahub.space/poster/small/" + id + L"/img"; };
        auto const withPoster = [&poster](std::wstring const& id, std::wstring const& name)
        {
            return Full(id, name, [&poster, &id](SearchTitle& t) { t.Poster = poster(id); });
        };
        std::vector<SearchTitle> const results{
            withPoster(L"tt1", L"Inside Christopher Nolan's Oppenheimer"),
            withPoster(L"tt2", L"IMDb Asks"),
            withPoster(L"tt3", L"Interstellar"),
            Full(L"tmdb:4", L"Some Short", [&noPoster](SearchTitle& t) { t.Poster = noPoster; }),
            withPoster(L"tt5", L"Inception"),
        };
        std::vector<SearchAnswer> const answers{ Answer(CinemetaMovies, results, 0) };
        // The links worth checking: those of the titles a reveal would show, in order, up to the limit.
        Require(RevealPosterLinks(answers, Parts(L"christopher nolan"), SearchQueryMeaning::Person, 24)
                    == std::vector<std::wstring>{ poster(L"tt1"), poster(L"tt2"), poster(L"tt3"), poster(L"tt5") },
            "the poster links to check were not those of the titles shown");
        Require(RevealPosterLinks(answers, Parts(L"christopher nolan"), SearchQueryMeaning::Person, 2).size() <= 2,
            "more poster links than the limit were checked");

        // A link known to lead nowhere: the title found by cast or crew goes,
        // and one named like the query stays but after those with artwork.
        auto marked = results;
        marked[0].PosterMissing = true;
        marked[1].PosterMissing = true;
        std::vector<SearchAnswer> const checked{ Answer(CinemetaMovies, marked, 0) };
        SearchResultBoard board;
        auto const revealed = board.Reveal(checked, Parts(L"christopher nolan"), SearchQueryMeaning::Person);
        Require(IdsOf(revealed.Titles) == std::vector<std::wstring>{ L"tt1", L"tt3", L"tt5" },
            "a title whose poster link leads nowhere was laid out among the person's films");

        // Another catalog's copy with a poster that draws clears the mark.
        auto const merged = HaloDesktop::Services::MergeCopies(
            marked[1], Full(L"tt2", L"IMDb Asks", [](SearchTitle& t) { t.Poster = L"https://image.tmdb.org/t/p/w500/asks.jpg"; }));
        Require(!merged.PosterMissing && merged.Poster == L"https://image.tmdb.org/t/p/w500/asks.jpg",
            "a copy with a poster that draws did not replace the missing one");
        auto const still = HaloDesktop::Services::MergeCopies(marked[1], marked[1]);
        Require(still.PosterMissing, "a title lost its missing-poster mark without a better poster");
    }

    void TestCutoffsDropUnrelated()
    {
        std::vector<SearchTitle> const results{
            Full(L"tmdb:1", L"Doctor Strange"), Full(L"tmdb:2", L"Faust and the Devil"), Full(L"tmdb:3", L"Dr. Strange") };
        Require(IdsOf(Refined(results, L"doctor strange")) == std::vector<std::wstring>{ L"tmdb:1", L"tmdb:3" },
            "a result sharing no word with the query stayed");
        // An addon answering in another language names nothing: all of it stays.
        std::vector<SearchTitle> const localized{
            Full(L"tmdb:1", L"डॉक्टर स्ट्रेंज"),
            Full(L"tmdb:2", L"कुछ और") };
        Require(Refined(localized, L"doctor strange").size() == 2, "an answer in another language was emptied");
    }

    // The titles of the year-and-artwork case: one without a poster, one with
    // neither poster nor year, one without a year, and complete ones.
    std::vector<SearchTitle> YearAndArtworkTitles()
    {
        std::wstring const noPoster = L"https://image.tmdb.org/t/p/w500null";
        return {
            Full(L"tmdb:1", L"Doctor Strange Love", [&noPoster](SearchTitle& t) { t.Poster = noPoster; }),
            Full(L"tmdb:2", L"Untitled Doctor Strange Movie", [&noPoster](SearchTitle& t) { t.Poster = noPoster; t.ReleaseInfo = L""; }),
            Full(L"tt3", L"Doctor Strange 3", [](SearchTitle& t) { t.ReleaseInfo.reset(); t.Poster = L"https://images.metahub.space/poster/small/tt3/img"; }),
            Full(L"tt4", L"Doctor Strange: Multiversal Madness", [](SearchTitle& t) { t.Poster = L"https://images.metahub.space/poster/small/tt4/img"; }),
            Full(L"tmdb:5", L"Doctor Strange in the Multiverse of Madness"),
            Full(L"tmdb:6", L"The Strange Case of Doctor Rx"),
        };
    }

    void TestCutoffsYearAndArtwork()
    {
        // Without a poster and without a year a title always goes; without
        // only one of them it stays; an IMDb title's poster is metahub's.
        Require(SortedIds(Refined(YearAndArtworkTitles(), L"doctor strange")) == std::vector<std::wstring>{
                    L"tmdb:1", L"tmdb:5", L"tmdb:6", L"tt3", L"tt4" },
            "a title without a year and artwork stayed, or one with either went");
    }

    void TestCutoffsArtlessBesideScreenful()
    {
        std::vector<SearchTitle> complete;
        for (int index = 0; index < 8; ++index)
        {
            complete.push_back(Full(L"tmdb:" + std::to_wstring(index), L"Doctor Strange " + std::to_wstring(index)));
        }
        auto const artless = Full(L"tmdb:99", L"Doctor Strange Love", [](SearchTitle& t) { t.Poster = L"https://image.tmdb.org/t/p/w500null"; });
        auto all = complete;
        all.push_back(artless);
        auto const refined = Refined(all, L"doctor strange");
        Require(std::none_of(refined.begin(), refined.end(), [](SearchTitle const& t) { return t.Id == L"tmdb:99"; }),
            "an artless title stayed beside a screenful of complete ones");
        // One short of a screenful: the artless one is still worth listing, last.
        std::vector<SearchTitle> fewer(complete.begin() + 1, complete.end());
        fewer.push_back(artless);
        auto const kept = Refined(fewer, L"doctor strange");
        Require(!kept.empty() && kept.back().Id == L"tmdb:99", "an artless title was dropped short of a screenful");
    }

    void TestRankingRelevanceBeforeCompleteness()
    {
        // What was typed ranks first: a complete title that only holds the
        // words comes after incomplete ones that begin with them.
        Require(IdsOf(Refined(YearAndArtworkTitles(), L"doctor strange")) == std::vector<std::wstring>{
                    L"tt4", L"tmdb:5", L"tmdb:1", L"tt3", L"tmdb:6" },
            "completeness was ranked ahead of how well the name answers the query");
    }

    void TestParseSearchQueryYear()
    {
        using HaloDesktop::Services::ParseSearchQuery;
        auto const dune = ParseSearchQuery(L"  dune   2021 ", 2026);
        Require(dune.Full == L"dune   2021" && dune.Name == L"dune" && dune.Year == 2021, "a trailing year was not read apart");
        auto const film = ParseSearchQuery(L"1917", 2026);
        Require(film.Name == L"1917" && !film.Year, "a query that is only a year lost its name");
        auto const blade = ParseSearchQuery(L"blade runner 2049", 2026);
        Require(blade.Name == L"blade runner 2049" && !blade.Year, "a number past next year was read as a year");
        auto const wonder = ParseSearchQuery(L"wonder woman 1984", 2026);
        Require(wonder.Name == L"wonder woman" && wonder.Year == 1984 && wonder.Full == L"wonder woman 1984",
            "a year that is part of the name was not offered both ways");
        Require(!ParseSearchQuery(L"dune 1887", 2026).Year, "a year before film was read as a year");
        Require(ParseSearchQuery(L"dune 2028", 2026).Year == 2028, "a year two ahead was not read as a year");
        Require(!ParseSearchQuery(L"dune 12021", 2026).Year, "a five-digit number was read as a year");
        // "Wonder Woman 1984" is still named exactly what was typed.
        std::vector<SearchAnswer> const answers{
            Answer(CinemetaMovies, { Full(L"tt7126948", L"Wonder Woman 1984", [](SearchTitle& t) { t.ReleaseInfo = L"2020"; }) }, 0) };
        auto const ranked = HaloDesktop::Services::RankForReveal(answers, wonder);
        Require(ranked.size() == 1 && ranked[0].Tier == 0, "a name holding a year did not match the full query");
    }

    void TestYearMatchBreaksTies()
    {
        auto const old = Full(L"tt0087182", L"Dune", [](SearchTitle& t) { t.ReleaseInfo = L"1984"; });
        auto const recent = Full(L"tt1160419", L"Dune", [](SearchTitle& t) { t.ReleaseInfo = L"2021"; });
        std::vector<SearchAnswer> const answers{ Answer(CinemetaMovies, { old, recent }, 0) };
        Require(IdsOf(Ranked(answers, L"dune 2021")) == std::vector<std::wstring>{ L"tt1160419", L"tt0087182" },
            "the typed year did not break a tie");
        Require(IdsOf(Ranked(answers, L"dune")) == std::vector<std::wstring>{ L"tt0087182", L"tt1160419" },
            "without a year the addon's own order did not stand");
    }

    void TestConsensusAndPosition()
    {
        auto const covenant = Full(L"tt2316204", L"Alien Covenant");
        auto const resurrection = Full(L"tt0118583", L"Alien Resurrection");
        auto const nation = Full(L"tt0094631", L"Alien Nation");
        auto const romulus = Full(L"tt18412256", L"Alien Romulus");
        std::vector<SearchAnswer> const answers{
            Answer(CinemetaMovies, { resurrection, covenant, romulus }, 0),
            Answer(TmdbMovies, { nation, covenant }, 1) };
        auto const ranked = HaloDesktop::Services::RankForReveal(answers, Parts(L"alien"));
        // Listed by two catalogs first; then the better place in a catalog's
        // own list, then the better addon.
        Require(IdsOf(Titles(ranked)) == std::vector<std::wstring>{ L"tt2316204", L"tt0118583", L"tt0094631", L"tt18412256" },
            "copies, then position, then addon rank did not order equal names");
        Require(ranked[0].Copies == 2 && ranked[0].BestPosition == 1 && ranked[0].AddonRank == 0,
            "a merged title's copies, position or rank were wrong");
        // The order the answers arrive in does not change the ranking.
        std::vector<SearchAnswer> const reversed{ answers[1], answers[0] };
        Require(IdsOf(Ranked(reversed, L"alien")) == IdsOf(Titles(ranked)), "the arrival order changed the ranking");
    }

    void TestRevealDecision()
    {
        using HaloDesktop::Services::DecideReveal;
        using HaloDesktop::Services::RevealDecision;
        using std::chrono::milliseconds;
        Require(DecideReveal(0, 3, 0, milliseconds{ 10 }) == RevealDecision::Reveal, "every catalog settled did not reveal at once");
        Require(DecideReveal(0, 0, 0, milliseconds{ 0 }) == RevealDecision::Reveal, "no catalog to ask did not reveal");
        Require(DecideReveal(2, 3, 1, milliseconds{ 249 }) == RevealDecision::Wait, "an answer revealed before the floor");
        Require(DecideReveal(1, 3, 2, milliseconds{ 600 }) == RevealDecision::Wait, "a catalog still out was not waited for");
        Require(DecideReveal(1, 3, 2, milliseconds{ 1200 }) == RevealDecision::Reveal, "the deadline did not reveal");
        Require(DecideReveal(1, 3, 0, milliseconds{ 5000 }) == RevealDecision::Wait, "the deadline revealed with no titles");
        Require(DecideReveal(1, 3, 1, milliseconds{ 5000 }) == RevealDecision::Reveal, "the first titles after the deadline did not reveal");
    }

    void TestLateAnswerOnlyAppends()
    {
        SearchResultBoard board;
        std::vector<SearchAnswer> const first{
            Answer(TmdbMovies, { Full(L"tt1", L"Doctor Strange"), Full(L"tt2", L"Doctor Strange 2") }, 1) };
        auto const revealed = board.Reveal(first, Parts(L"doctor strange"));
        Require(revealed.TopMatch && revealed.TopMatch->Id == L"tt1" && IdsOf(revealed.Titles) == std::vector<std::wstring>{ L"tt2" },
            "the reveal was not as expected");
        // A better addon answering late, with a better name, still lands after the last card.
        auto const diff = board.Absorb(Answer(CinemetaMovies, {
            Full(L"tt9", L"Doctor Strange", [](SearchTitle& t) { t.ReleaseInfo = L"1978"; }), Full(L"tt3", L"Doctor Strange 3") }, 0));
        Require(diff.Updates.empty() && !diff.TopMatchUpdate, "a late answer of new titles changed a shown one");
        Require(IdsOf(diff.Appends) == std::vector<std::wstring>{ L"tt9", L"tt3" }, "late titles were not appended in the addon's order");
        Require(IdsOf(board.Titles()) == std::vector<std::wstring>{ L"tt2", L"tt9", L"tt3" }, "a late answer moved a shown card");
        Require(board.TopMatch() && board.TopMatch()->Id == L"tt1", "a late answer replaced the top match");
        // Before a reveal nothing is absorbed.
        SearchResultBoard unrevealed;
        Require(unrevealed.Absorb(first[0]).Empty() && unrevealed.Titles().empty(), "an unrevealed board absorbed an answer");
    }

    void TestLateCopyFoldsFieldsInPlace()
    {
        SearchResultBoard board;
        auto shown = Full(L"tmdb:5", L"Doctor Strange in the Multiverse of Madness");
        shown.ImdbId = L"tt9419884";
        auto const top = Full(L"tt1211837", L"Doctor Strange");
        std::vector<SearchAnswer> const first{ Answer(TmdbMovies, { top, shown }, 1) };
        static_cast<void>(board.Reveal(first, Parts(L"doctor strange")));

        // Cinemeta's copy names the title by its IMDb id and knows a rating and a description.
        auto late = Dated(L"tt9419884", L"Doctor Strange in the Multiverse of Madness", L"2022");
        late.Rating = L"6.9";
        late.Description = L"Wanda.";
        auto topCopy = Dated(L"tt1211837", L"Doctor Strange", L"2016");
        topCopy.Rating = L"7.5";
        auto const diff = board.Absorb(Answer(CinemetaMovies, { late, topCopy }, 0));
        Require(diff.Appends.empty(), "a late copy of a shown title was appended");
        Require(diff.Updates.size() == 1 && diff.Updates[0].first == 0, "a late copy did not update its card in place");
        auto const& updated = diff.Updates[0].second;
        Require(updated.Id == L"tmdb:5" && updated.Type == L"movie" && updated.Name == shown.Name,
            "a late copy changed the shown card's id, kind or name");
        Require(updated.Rating == L"6.9" && updated.Description == L"Wanda." && updated.Poster == shown.Poster,
            "a late copy's details were not folded in, or replaced a poster that loads");
        Require(board.Titles()[0] == updated, "the board did not keep the updated card");
        Require(diff.TopMatchUpdate && diff.TopMatchUpdate->Id == L"tt1211837" && diff.TopMatchUpdate->Rating == L"7.5",
            "a late copy of the top match did not fill it in");
        // The same answer again changes nothing.
        Require(board.Absorb(Answer(CinemetaMovies, { late, topCopy }, 0)).Empty(), "an answer already folded in changed something");
    }

    void TestLateAnswerUsesFrozenCutoffs()
    {
        std::vector<SearchTitle> named;
        for (int index = 0; index < 8; ++index)
        {
            named.push_back(Full(L"tmdb:" + std::to_wstring(index), L"Doctor Strange " + std::to_wstring(index)));
        }
        auto const sleep = Full(L"tmdb:90", L"Doctor Sleep");
        auto const faust = Full(L"tmdb:91", L"Faust and the Devil");
        auto const artless = Full(L"tmdb:92", L"Doctor Strange Love", [](SearchTitle& t) { t.Poster = L"https://image.tmdb.org/t/p/w500null"; });

        SearchResultBoard full;
        std::vector<SearchAnswer> const screenful{ Answer(TmdbMovies, named, 1) };
        auto const revealed = full.Reveal(screenful, Parts(L"doctor strange"));
        Require(revealed.Cutoffs.DropUnrelated && revealed.Cutoffs.DropSomeWords && revealed.Cutoffs.DropArtless,
            "a screenful of complete, well-named titles did not set every cutoff");
        Require(full.Absorb(Answer(CinemetaMovies, { sleep, faust, artless }, 0)).Empty(),
            "a late title the reveal's cutoffs drop was appended");

        // Revealed with few titles, the same late titles pass, and a late
        // screenful does not tighten the cutoffs afterwards.
        SearchResultBoard few;
        std::vector<SearchAnswer> const two{ Answer(TmdbMovies, { named[0], named[1] }, 1) };
        auto const loose = few.Reveal(two, Parts(L"doctor strange"));
        Require(loose.Cutoffs.DropUnrelated && !loose.Cutoffs.DropSomeWords && !loose.Cutoffs.DropArtless,
            "two titles set the wrong cutoffs");
        std::vector<SearchTitle> late(named.begin() + 2, named.end());
        late.push_back(sleep);
        late.push_back(faust);
        late.push_back(artless);
        auto const diff = few.Absorb(Answer(CinemetaMovies, late, 0));
        Require(IdsOf(diff.Appends) == std::vector<std::wstring>{
                    L"tmdb:2", L"tmdb:3", L"tmdb:4", L"tmdb:5", L"tmdb:6", L"tmdb:7", L"tmdb:90", L"tmdb:92" },
            "late titles were not judged by the cutoffs frozen at the reveal");
    }

    void TestVisiblePrefixNeverChanges()
    {
        // Five catalogs whose answers overlap: the same titles under other
        // ids, with and without years, posters and ratings, plus their own.
        auto withImdb = [](SearchTitle title, std::wstring imdb) { title.ImdbId = std::move(imdb); return title; };
        auto rated = [](SearchTitle title, std::wstring rating) { title.Rating = std::move(rating); return title; };
        std::vector<SearchAnswer> const answers{
            Answer(Target(L"c/movie", L"Cinemeta"), {
                Full(L"tt1160419", L"Dune", [](SearchTitle& t) { t.ReleaseInfo = L"2021"; }),
                Full(L"tt15239678", L"Dune: Part Two"),
                Full(L"tt0087182", L"Dune", [](SearchTitle& t) { t.ReleaseInfo = L"1984"; }) }, 0),
            Answer(Target(L"c/series", L"Cinemeta", L"series"), {
                Dated(L"tt10466872", L"Dune: Prophecy", L"2024", L"series"),
                Dated(L"tt0142032", L"Dune", L"2000", L"series") }, 0),
            Answer(Target(L"t/movie", L"TMDB"), {
                rated(withImdb(Full(L"tmdb:438631", L"Dune", [](SearchTitle& t) { t.ReleaseInfo = L"2021"; }), L"tt1160419"), L"7.8"),
                withImdb(Full(L"tmdb:693134", L"Dune: Part Two"), L"tt15239678"),
                Full(L"tmdb:1", L"Dune Drifter") }, 1),
            Answer(Target(L"m/movie", L"MediaFusion"), {
                Named(L"mf1706190", L"Dune (2021)"),
                Named(L"mf17", L"Dune World"),
                Full(L"tmdb:2", L"Faust and the Devil") }, 2),
            Answer(Target(L"o/movie", L"Other"), {
                rated(Dated(L"tt0087182", L"Dune", L"1984"), L"6.3"),
                Full(L"o:3", L"Dune Messiah"),
                Full(L"o:4", L"Children of Dune") }, 3),
        };
        for (std::uint32_t seed = 1; seed <= 40; ++seed)
        {
            std::vector<SearchAnswer> order = answers;
            std::mt19937 random{ seed };
            std::shuffle(order.begin(), order.end(), random);
            auto const revealedCount = static_cast<std::size_t>(seed % order.size()) + 1;
            for (auto const& query : { std::wstring{ L"dune" }, std::wstring{ L"dune 2021" } })
            {
                SearchResultBoard board;
                auto const revealed = board.Reveal(
                    std::span<SearchAnswer const>{ order.data(), revealedCount }, Parts(query));
                auto shownKeys = Keys(revealed.Titles);
                auto const topKey = revealed.TopMatch ? HaloDesktop::Services::TitleKey(*revealed.TopMatch) : std::wstring{};
                for (auto index = revealedCount; index < order.size(); ++index)
                {
                    auto const before = board.Titles().size();
                    auto const diff = board.Absorb(order[index]);
                    auto const nowKeys = Keys(board.Titles());
                    Require(nowKeys.size() == before + diff.Appends.size(), "a late answer removed a card or appended out of its diff");
                    Require(std::equal(shownKeys.begin(), shownKeys.end(), nowKeys.begin()), "a late answer moved or replaced a shown card");
                    for (auto const& [at, title] : diff.Updates)
                    {
                        Require(at < before && HaloDesktop::Services::TitleKey(title) == nowKeys[at], "an update named another card");
                    }
                    auto const nowTop = board.TopMatch() ? HaloDesktop::Services::TitleKey(*board.TopMatch()) : std::wstring{};
                    Require(nowTop == topKey, "a late answer changed the top match");
                    shownKeys = nowKeys;
                }
            }
        }
    }

    void TestIsSearchedCatalogType()
    {
        using HaloDesktop::Services::IsSearchedCatalogType;
        Require(IsSearchedCatalogType(L"movie") && IsSearchedCatalogType(L"series"), "films or series were not searched");
        Require(!IsSearchedCatalogType(L"tv") && !IsSearchedCatalogType(L"channel") && !IsSearchedCatalogType(L"other")
                && !IsSearchedCatalogType(L"") && !IsSearchedCatalogType(L"Movie"),
            "a catalog that is not films or series was searched");
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
    TestFailedCatalogsReported();
    TestUniqueTitles();
    TestCopiesMergeAcrossIds();
    TestCopiesKeepDifferentYearsKinds();
    TestByRelevance();
    TestRelevanceIgnoresCaseAccentsPunctuation();
    TestRevealTopMatchPrefersExactName();
    TestRevealTopMatchIgnoresFailedAndEmpty();
    TestLooseNameIsNoTopMatch();
    TestCutoffsDropUnrelated();
    TestPersonQueryKeepsTheirWork();
    TestMissingPosterLinks();
    TestCutoffsYearAndArtwork();
    TestCutoffsArtlessBesideScreenful();
    TestRankingRelevanceBeforeCompleteness();
    TestTmdbMissingImage();
    TestCopiesEnrichByImdbId();
    TestShortForms();
    TestScreenfulDropsLooseNames();
    TestFileNameEntry();
    TestDrawnPosterIsNoPoster();
    TestProductCodes();
    TestParseSearchQueryYear();
    TestYearMatchBreaksTies();
    TestConsensusAndPosition();
    TestRevealDecision();
    TestLateAnswerOnlyAppends();
    TestLateCopyFoldsFieldsInPlace();
    TestLateAnswerUsesFrozenCutoffs();
    TestVisiblePrefixNeverChanges();
    TestIsSearchedCatalogType();
    TestLibraryMatches();
    TestSuggestedSearches();
    TestNotesAndQueryLimits();
    TestFolding();
    TestSearchHistory();
}
