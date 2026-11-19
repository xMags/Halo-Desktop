#pragma once

#include "Services/SearchResultsPolicy.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace HaloDesktop::Services
{
    // How the catalogs' answers to one query become one ranked list, once,
    // when the results are revealed. No UI or WinRT types in the interface,
    // so the tests compile this by path.

    // One catalog's answer, with where its addon stands in the effective order.
    struct SearchAnswer final
    {
        SearchTarget Target;
        std::vector<SearchTitle> Titles;
        std::size_t AddonRank{};
    };

    // What was typed, trimmed, and its name and year read apart: "dune 2021"
    // is the name "dune" from 2021.
    struct SearchQueryParts final
    {
        std::wstring Full;
        std::wstring Name;
        std::optional<int> Year;
    };

    // A trailing four-digit number from 1888 to `currentYear` + 2 is the year,
    // and the name is the query without it. A query that is only a year keeps
    // it as the name, with no year.
    [[nodiscard]] SearchQueryParts ParseSearchQuery(std::wstring_view query, int currentYear);
    // The same, against this year on the system clock.
    [[nodiscard]] SearchQueryParts ParseSearchQuery(std::wstring_view query);

    // One title of the merged answers and what it is ranked by.
    struct RankedTitle final
    {
        SearchTitle Title;
        // The better of the title's name relevance to the full query and to
        // its name alone (see NameRelevance), lower is better.
        int Tier{};
        // The query has a year and the title's release line has the same one.
        bool YearMatches{};
        // A release year and a poster that can be drawn.
        bool Complete{};
        // How many catalogs listed it.
        int Copies{};
        // The best index it had in any catalog's own list.
        std::size_t BestPosition{};
        // The best rank of any addon that listed it.
        std::size_t AddonRank{};
        // Every identity any of its copies is known by (TitleIdentities), so
        // a later copy of any of them finds this title.
        std::vector<std::wstring> Identities;
    };

    // The thresholds the revealed list was judged by, kept so that later
    // answers are judged by the same ones.
    struct RevealCutoffs final
    {
        // Some title shares a word with the query, so those sharing none go.
        bool DropUnrelated{};
        // Enough titles hold every word, so those holding only some go.
        bool DropSomeWords{};
        // Enough complete titles, so those without a poster go.
        bool DropArtless{};
        // The query is a person's name: the complete titles an addon found
        // for it by cast or crew stay although their names share no word.
        bool KeepCredited{};
    };

    // What the words typed stand for, as far as the search could tell.
    enum class SearchQueryMeaning
    {
        // A title's name, or nothing known.
        Title,
        // Exactly some person's name.
        Person,
    };

    // A person's name counts as one only while no title is named exactly
    // that. Then the person's work stays (KeepCredited) and titles holding
    // only some of the name's words go, however few hold all of them.
    [[nodiscard]] RevealCutoffs ComputeCutoffs(
        std::span<RankedTitle const> ranked,
        SearchQueryMeaning meaning = SearchQueryMeaning::Title);
    // A title without a poster and without a year never passes.
    [[nodiscard]] bool PassesCutoffs(RankedTitle const& title, RevealCutoffs const& cutoffs);

    // Whether a title has no poster worth drawing: none, or a stand-in, unless
    // it is named by its IMDb id (whose poster metahub draws); or a poster
    // link that was tried and leads to no image (PosterMissing).
    [[nodiscard]] bool IsArtless(SearchTitle const& title);
    // The poster links of the first `limit` titles a reveal of these answers
    // would show, in screen order: the ones worth knowing to be missing
    // before the results are laid out.
    [[nodiscard]] std::vector<std::wstring> RevealPosterLinks(
        std::span<SearchAnswer const> answers,
        SearchQueryParts const& query,
        SearchQueryMeaning meaning,
        std::size_t limit);
    // A single title ranked against the query, as one copy at `position` of
    // an addon at `addonRank`.
    [[nodiscard]] RankedTitle RankOne(
        SearchTitle title,
        SearchQueryParts const& query,
        std::size_t position,
        std::size_t addonRank);

    // Every answer as one list: product codes dropped, years read out of
    // names, copies of one title merged (MergeCopies), then ordered by tier,
    // a matching year (when the query has one), completeness, more copies,
    // a better position in a catalog's own list, and a better addon rank.
    // Copies are merged in turns by position and then addon rank, so the
    // result does not depend on the order the answers are passed in.
    [[nodiscard]] std::vector<RankedTitle> RankForReveal(
        std::span<SearchAnswer const> answers,
        SearchQueryParts const& query);

    // The first title when it is named by the query (tier 0 or 1) and, when
    // the query has a year, from that year.
    [[nodiscard]] std::optional<std::size_t> ChooseRevealTopMatch(
        std::span<RankedTitle const> ranked,
        SearchQueryParts const& query);
}
