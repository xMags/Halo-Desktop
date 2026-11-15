#include "Services/HomeShelfLoadPolicy.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace HaloDesktop::Services
{
    std::vector<HomeShelfTicket> HomeShelfBoard::Plan(
        std::span<std::wstring const> keys,
        HomeShelfRefetch refetch)
    {
        std::vector<HomeShelfSlot> slots;
        slots.reserve(keys.size());
        std::vector<HomeShelfTicket> tickets;
        std::unordered_set<std::wstring> placed;
        for (auto const& key : keys)
        {
            if (!placed.insert(key).second)
            {
                continue;
            }
            auto const kept = std::find_if(m_slots.begin(), m_slots.end(), [&key](HomeShelfSlot const& slot)
            {
                return slot.Key == key;
            });
            if (kept == m_slots.end())
            {
                HomeShelfSlot slot;
                slot.Key = key;
                slot.State = HomeShelfState::Loading;
                slot.Fetching = true;
                slot.Generation = ++m_lastGeneration;
                tickets.push_back({ slot.Key, slot.Generation });
                slots.push_back(std::move(slot));
                continue;
            }

            auto slot = std::move(*kept);
            if (refetch == HomeShelfRefetch::RefetchAll)
            {
                // A newer request for the same shelf: whatever the older one
                // still brings back is stale and will be discarded.
                slot.Generation = ++m_lastGeneration;
                slot.Fetching = true;
                // Titles on screen stay until the answer; an empty shelf stays
                // hidden; a failed shelf has nothing, so it loads again.
                if (slot.State == HomeShelfState::Failed)
                {
                    slot.State = HomeShelfState::Loading;
                }
                tickets.push_back({ slot.Key, slot.Generation });
            }
            slots.push_back(std::move(slot));
        }
        m_slots = std::move(slots);
        m_listKnown = true;
        return tickets;
    }

    HomeShelfApply HomeShelfBoard::Complete(HomeShelfTicket const& ticket, HomeShelfAnswer answer)
    {
        auto const slot = std::find_if(m_slots.begin(), m_slots.end(), [&ticket](HomeShelfSlot const& candidate)
        {
            return candidate.Key == ticket.Key;
        });
        if (slot == m_slots.end() || slot->Generation != ticket.Generation || !slot->Fetching)
        {
            return HomeShelfApply::Discarded;
        }

        slot->Fetching = false;
        switch (answer)
        {
        case HomeShelfAnswer::Items:
            slot->State = HomeShelfState::Loaded;
            slot->LastFailed = false;
            return HomeShelfApply::Applied;
        case HomeShelfAnswer::Empty:
            slot->State = HomeShelfState::Empty;
            slot->LastFailed = false;
            return HomeShelfApply::Applied;
        case HomeShelfAnswer::Failed:
            break;
        }

        slot->LastFailed = true;
        if (slot->State == HomeShelfState::Loaded)
        {
            return HomeShelfApply::KeptPrevious;
        }
        slot->State = HomeShelfState::Failed;
        return HomeShelfApply::Applied;
    }

    void HomeShelfBoard::Reset() noexcept
    {
        // The generation counter is not reset: a ticket issued before this can
        // never match a slot laid out after it, even one with the same key.
        m_slots.clear();
        m_listKnown = false;
    }

    bool HomeShelfBoard::ListKnown() const noexcept
    {
        return m_listKnown;
    }

    std::span<HomeShelfSlot const> HomeShelfBoard::Slots() const noexcept
    {
        return m_slots;
    }

    std::optional<std::size_t> HomeShelfBoard::IndexOf(std::wstring const& key) const noexcept
    {
        for (std::size_t index = 0; index < m_slots.size(); ++index)
        {
            if (m_slots[index].Key == key)
            {
                return index;
            }
        }
        return std::nullopt;
    }

    bool HomeShelfBoard::Settled() const noexcept
    {
        return std::none_of(m_slots.begin(), m_slots.end(), [](HomeShelfSlot const& slot) { return slot.Fetching; });
    }

    bool HomeShelfBoard::EveryShelfFailed() const noexcept
    {
        return !m_slots.empty() && Settled()
            && std::all_of(m_slots.begin(), m_slots.end(), [](HomeShelfSlot const& slot) { return slot.LastFailed; });
    }

    bool HomeShelfShowsItems(HomeShelfState state) noexcept
    {
        return state == HomeShelfState::Loaded;
    }

    bool HomeShelfPending(HomeShelfSlot const& slot) noexcept
    {
        return slot.State == HomeShelfState::Loading;
    }

    FeaturedSettleResult SelectSettledFeatured(
        ::HaloDesktop::ViewModels::HomeFilter filter,
        std::span<FeaturedPoolSlot const> slots,
        std::size_t count)
    {
        std::vector<::HaloDesktop::ViewModels::FeaturedCandidate> pool;
        bool allAnswered = true;
        for (auto const& slot : slots)
        {
            if (slot.Pending)
            {
                allAnswered = false;
                break;
            }
            pool.insert(pool.end(), slot.Candidates.begin(), slot.Candidates.end());
        }

        auto picks = ::HaloDesktop::ViewModels::SelectFeaturedItems(filter, pool, count);
        auto const fullOfBackdrops = count > 0 && picks.size() == count
            && std::all_of(picks.begin(), picks.end(), [&pool](std::size_t index) { return pool[index].HasBackdrop; });
        FeaturedSettleResult result;
        result.Settled = allAnswered || fullOfBackdrops;
        if (result.Settled)
        {
            result.Picks = std::move(picks);
        }
        return result;
    }

    std::vector<HomeRowEdit> PlanHomeRowEdits(
        std::span<HomeRowIdentity const> current,
        std::span<HomeRowIdentity const> next)
    {
        // The list as the edits leave it, as indices into `current`, or npos
        // for a row already inserted from `next`.
        constexpr auto Inserted = static_cast<std::size_t>(-1);
        std::vector<std::size_t> working(current.size());
        for (std::size_t index = 0; index < working.size(); ++index)
        {
            working[index] = index;
        }

        std::vector<HomeRowEdit> edits;
        std::size_t position = 0;
        for (std::size_t source = 0; source < next.size(); ++source)
        {
            auto const& wanted = next[source];
            auto found = working.size();
            for (auto candidate = position; candidate < working.size(); ++candidate)
            {
                if (working[candidate] != Inserted && current[working[candidate]].Key == wanted.Key)
                {
                    found = candidate;
                    break;
                }
            }

            if (found == working.size())
            {
                edits.push_back({ HomeRowEditKind::Insert, position, source });
                working.insert(working.begin() + static_cast<std::ptrdiff_t>(position), Inserted);
                ++position;
                continue;
            }

            for (auto removed = position; removed < found; ++removed)
            {
                edits.push_back({ HomeRowEditKind::Remove, position, 0 });
            }
            working.erase(
                working.begin() + static_cast<std::ptrdiff_t>(position),
                working.begin() + static_cast<std::ptrdiff_t>(found));
            if (current[working[position]].Signature != wanted.Signature)
            {
                edits.push_back({ HomeRowEditKind::Replace, position, source });
            }
            ++position;
        }
        while (working.size() > position)
        {
            edits.push_back({ HomeRowEditKind::Remove, position, 0 });
            working.pop_back();
        }
        return edits;
    }
} // namespace HaloDesktop::Services
