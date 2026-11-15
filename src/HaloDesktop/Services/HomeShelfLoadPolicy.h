#pragma once

#include "ViewModels/HomeStatePolicy.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace HaloDesktop::Services
{
    // Home's catalog shelves load one by one, the way the reference's per-shelf
    // queries do (`home/Home.tsx`). The shelf list is laid out as soon as the
    // addons are known; every shelf then settles on its own. These rules hold
    // no UI or WinRT types so the tests can compile them by path.

    enum class HomeShelfState
    {
        // Nothing to show yet: the shelf draws its title over skeleton cards.
        Loading,
        // Titles to show. A refetch keeps them on screen until it answers.
        Loaded,
        // The catalog answered with no titles. The shelf is not drawn.
        Empty,
        // The catalog failed with nothing shown before it. Not drawn either.
        Failed,
    };

    enum class HomeShelfAnswer
    {
        Items,
        Empty,
        Failed,
    };

    // What became of an answer handed to the board.
    enum class HomeShelfApply
    {
        // Not the slot's newest request, or the slot is gone (a newer shelf
        // list, an account change). Nothing may change.
        Discarded,
        // The slot took the answer: store or drop its titles.
        Applied,
        // A refetch failed over titles already shown: they stay, unchanged.
        KeptPrevious,
    };

    // Which kept shelves a new plan asks again.
    enum class HomeShelfRefetch
    {
        // Only the catalog language changed: a shelf whose request is the same
        // keeps what it has, and one still in flight goes on.
        KeepAnswered,
        // Anything else (addons changed, a retry): every kept shelf asks again,
        // showing what it has meanwhile.
        RefetchAll,
    };

    // Issued for each request the board wants made. Its generation is unique
    // for the life of the board, so an answer can only ever land in the very
    // request that asked for it.
    struct HomeShelfTicket final
    {
        std::wstring Key;
        std::uint64_t Generation{};
    };

    struct HomeShelfSlot final
    {
        // The request's identity: addon, type, catalog and extras.
        std::wstring Key;
        HomeShelfState State{ HomeShelfState::Loading };
        // A request for this slot is outstanding.
        bool Fetching{};
        // The slot's last answer was a failure (kept titles or not).
        bool LastFailed{};
        std::uint64_t Generation{};
    };

    // The catalog shelves' load state, in shelf order. UI-thread only, like
    // the service that owns it; no locks.
    class HomeShelfBoard final
    {
    public:
        // Lays out a new shelf list. Order is the order of `keys`, and a slot
        // keeps its place for as long as this list stands. Duplicate keys keep
        // their first place only. Returns the requests to start, in shelf order.
        [[nodiscard]] std::vector<HomeShelfTicket> Plan(
            std::span<std::wstring const> keys,
            HomeShelfRefetch refetch);
        [[nodiscard]] HomeShelfApply Complete(HomeShelfTicket const& ticket, HomeShelfAnswer answer);
        // An account change: every slot goes, and every outstanding ticket is
        // retired with them.
        void Reset() noexcept;

        [[nodiscard]] bool ListKnown() const noexcept;
        [[nodiscard]] std::span<HomeShelfSlot const> Slots() const noexcept;
        [[nodiscard]] std::optional<std::size_t> IndexOf(std::wstring const& key) const noexcept;
        // No request outstanding.
        [[nodiscard]] bool Settled() const noexcept;
        // Settled with at least one shelf, every one of whose last answers was
        // a failure: the refresh as a whole failed.
        [[nodiscard]] bool EveryShelfFailed() const noexcept;

    private:
        std::vector<HomeShelfSlot> m_slots;
        std::uint64_t m_lastGeneration{};
        bool m_listKnown{};
    };

    // Whether a slot holds titles the page draws.
    [[nodiscard]] bool HomeShelfShowsItems(HomeShelfState state) noexcept;
    // A slot that has nothing to show and is still waiting for its answer.
    [[nodiscard]] bool HomeShelfPending(HomeShelfSlot const& slot) noexcept;

    // One shelf's contribution to the hero: whether it is still pending, and
    // its titles when it has answered.
    struct FeaturedPoolSlot final
    {
        bool Pending{};
        std::vector<::HaloDesktop::ViewModels::FeaturedCandidate> Candidates;
    };

    struct FeaturedSettleResult final
    {
        // The hero may show its picks: they can no longer change.
        bool Settled{};
        // Indices into the answered shelves' candidates laid end to end, in
        // shelf order. Empty while not settled.
        std::vector<std::size_t> Picks;
    };

    // The reference's settle rule (`FeaturedHero`): the picks come from the
    // shelves that have answered, in order, up to the first that has not; they
    // are shown once every shelf has answered, or once those shelves already
    // yield `count` picks that all have a backdrop, which no later shelf can
    // displace. A failed shelf answers with nothing.
    [[nodiscard]] FeaturedSettleResult SelectSettledFeatured(
        ::HaloDesktop::ViewModels::HomeFilter filter,
        std::span<FeaturedPoolSlot const> slots,
        std::size_t count);

    // A row of Home's shelf list: its identity, and a signature of what it
    // draws, so an unchanged row is left alone and keeps its scroll position.
    struct HomeRowIdentity final
    {
        std::wstring Key;
        std::wstring Signature;
    };

    enum class HomeRowEditKind
    {
        Remove,
        Insert,
        Replace,
    };

    struct HomeRowEdit final
    {
        HomeRowEditKind Kind{};
        // Where in the list as it stands when this edit is applied.
        std::size_t Index{};
        // The row of the new list that goes there (Insert and Replace).
        std::size_t Source{};
    };

    // The edits that turn `current` into `next`, applied in order. Rows keep
    // their relative order in both lists, so a row that stays is never moved:
    // rows that went are removed, new rows inserted where they belong, and a
    // kept row whose signature changed replaced in place. Keys are unique
    // within each list.
    [[nodiscard]] std::vector<HomeRowEdit> PlanHomeRowEdits(
        std::span<HomeRowIdentity const> current,
        std::span<HomeRowIdentity const> next);
} // namespace HaloDesktop::Services
