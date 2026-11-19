#include "Services/SearchResultBoard.h"

#include <algorithm>

namespace HaloDesktop::Services
{
    RevealedSet SearchResultBoard::Reveal(std::span<SearchAnswer const> answers, SearchQueryParts const& query)
    {
        m_query = query;
        auto ranked = RankForReveal(answers, query);
        m_cutoffs = ComputeCutoffs(ranked);
        std::erase_if(ranked, [this](RankedTitle const& title) { return !PassesCutoffs(title, m_cutoffs); });
        auto const top = ChooseRevealTopMatch(ranked, query);

        m_titles.clear();
        m_slots.clear();
        m_topMatch.reset();
        m_titles.reserve(ranked.size());
        for (std::size_t index = 0; index < ranked.size(); ++index)
        {
            auto& entry = ranked[index];
            if (top && index == *top)
            {
                m_topMatch = std::move(entry.Title);
                Claim(TopMatchSlot, entry.Identities);
                continue;
            }
            Claim(m_titles.size(), entry.Identities);
            m_titles.push_back(std::move(entry.Title));
        }
        m_revealed = true;
        return { m_titles, m_topMatch, m_cutoffs };
    }

    BoardDiff SearchResultBoard::Absorb(SearchAnswer const& late)
    {
        BoardDiff diff;
        if (!m_revealed)
        {
            return diff;
        }
        auto const firstAppend = m_titles.size();
        for (std::size_t position = 0; position < late.Titles.size(); ++position)
        {
            if (IsProductCodeName(late.Titles[position].Name)) continue;
            auto title = WithYearOutOfName(late.Titles[position]);
            auto const identities = TitleIdentities(title);
            if (auto const slot = Find(identities))
            {
                Claim(*slot, identities);
                auto& shown = *slot == TopMatchSlot ? *m_topMatch : m_titles[*slot];
                auto next = FillMissingDetails(shown, title);
                if (next == shown) continue;
                shown = std::move(next);
                Claim(*slot, TitleIdentities(shown));
                if (*slot == TopMatchSlot)
                {
                    diff.TopMatchUpdate = shown;
                }
                else if (*slot >= firstAppend)
                {
                    // Appended by this same answer: it is still news, as it now reads.
                    diff.Appends[*slot - firstAppend] = shown;
                }
                else if (auto const updated = std::find_if(diff.Updates.begin(), diff.Updates.end(), [index = *slot](auto const& entry)
                    {
                        return entry.first == index;
                    });
                    updated != diff.Updates.end())
                {
                    updated->second = shown;
                }
                else
                {
                    diff.Updates.emplace_back(*slot, shown);
                }
                continue;
            }
            auto ranked = RankOne(std::move(title), m_query, position, late.AddonRank);
            if (!PassesCutoffs(ranked, m_cutoffs)) continue;
            Claim(m_titles.size(), ranked.Identities);
            diff.Appends.push_back(ranked.Title);
            m_titles.push_back(std::move(ranked.Title));
        }
        return diff;
    }

    bool SearchResultBoard::Revealed() const noexcept { return m_revealed; }
    std::vector<SearchTitle> const& SearchResultBoard::Titles() const noexcept { return m_titles; }
    std::optional<SearchTitle> const& SearchResultBoard::TopMatch() const noexcept { return m_topMatch; }
    RevealCutoffs const& SearchResultBoard::Cutoffs() const noexcept { return m_cutoffs; }

    void SearchResultBoard::Claim(std::size_t slot, std::span<std::wstring const> identities)
    {
        // The first title an identity named keeps it, so a later copy can
        // never move an identity, and with it a match, to another card.
        for (auto const& identity : identities) m_slots.try_emplace(identity, slot);
    }

    std::optional<std::size_t> SearchResultBoard::Find(std::span<std::wstring const> identities) const
    {
        for (auto const& identity : identities)
        {
            if (auto const found = m_slots.find(identity); found != m_slots.end()) return found->second;
        }
        return std::nullopt;
    }
}
