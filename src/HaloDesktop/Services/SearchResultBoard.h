#pragma once

#include "Services/SearchRankingPolicy.h"
#include "Services/SearchResultsPolicy.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace HaloDesktop::Services
{
    // What one query's results showed when they were revealed.
    struct RevealedSet final
    {
        // In screen order, without the top match.
        std::vector<SearchTitle> Titles;
        std::optional<SearchTitle> TopMatch;
        RevealCutoffs Cutoffs;
    };

    // What a later answer changes on a revealed board: titles already shown
    // that gained details, in place, and new titles after the last one.
    struct BoardDiff final
    {
        // Board index and the title as it now reads (same id, kind and name).
        std::vector<std::pair<std::size_t, SearchTitle>> Updates;
        // In order; the first lands at the index after the board's last title.
        std::vector<SearchTitle> Appends;
        // The top match as it now reads, when a later copy filled it in.
        std::optional<SearchTitle> TopMatchUpdate;

        [[nodiscard]] bool Empty() const noexcept { return Updates.empty() && Appends.empty() && !TopMatchUpdate; }
    };

    // One query's revealed results, which only ever grow at the end once
    // shown: a later answer fills in the details of a title already shown
    // (never its id, kind, name or place) and appends titles that pass the
    // thresholds the reveal was judged by. Nothing is removed or reordered;
    // only a new board (a new query) lays the list out again. Pure and
    // single-threaded, like the rest of the search rules.
    class SearchResultBoard final
    {
    public:
        // Ranks every answer so far and keeps the result. Any earlier reveal
        // on this board is replaced.
        RevealedSet Reveal(
            std::span<SearchAnswer const> answers,
            SearchQueryParts const& query,
            SearchQueryMeaning meaning = SearchQueryMeaning::Title);
        // A catalog that answered after the reveal. Ignored before one.
        BoardDiff Absorb(SearchAnswer const& late);

        [[nodiscard]] bool Revealed() const noexcept;
        [[nodiscard]] std::vector<SearchTitle> const& Titles() const noexcept;
        [[nodiscard]] std::optional<SearchTitle> const& TopMatch() const noexcept;
        [[nodiscard]] RevealCutoffs const& Cutoffs() const noexcept;

    private:
        // Where an identity's title sits: a board index, or the top match.
        static constexpr std::size_t TopMatchSlot = static_cast<std::size_t>(-1);

        void Claim(std::size_t slot, std::span<std::wstring const> identities);
        [[nodiscard]] std::optional<std::size_t> Find(std::span<std::wstring const> identities) const;

        bool m_revealed{};
        SearchQueryParts m_query;
        RevealCutoffs m_cutoffs;
        std::vector<SearchTitle> m_titles;
        std::optional<SearchTitle> m_topMatch;
        std::unordered_map<std::wstring, std::size_t> m_slots;
    };
}
