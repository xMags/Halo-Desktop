#include "HomeShelfLoadTests.h"

#include "Services/HomeShelfLoadPolicy.h"

#include <cstddef>
#include <initializer_list>
#include <stdexcept>
#include <string>
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

    using HaloDesktop::Services::HomeRowEditKind;
    using HaloDesktop::Services::HomeRowIdentity;
    using HaloDesktop::Services::HomeShelfAnswer;
    using HaloDesktop::Services::HomeShelfApply;
    using HaloDesktop::Services::HomeShelfBoard;
    using HaloDesktop::Services::HomeShelfRefetch;
    using HaloDesktop::Services::HomeShelfState;
    using HaloDesktop::Services::HomeShelfTicket;
    using HaloDesktop::ViewModels::FeaturedCandidate;
    using HaloDesktop::ViewModels::HomeFilter;
    using HaloDesktop::ViewModels::HomeMediaKind;

    std::vector<std::wstring> Keys(std::initializer_list<wchar_t const*> keys)
    {
        std::vector<std::wstring> result;
        for (auto const* key : keys)
        {
            result.emplace_back(key);
        }
        return result;
    }

    HomeShelfTicket TicketFor(std::vector<HomeShelfTicket> const& tickets, std::wstring const& key)
    {
        for (auto const& ticket : tickets)
        {
            if (ticket.Key == key)
            {
                return ticket;
            }
        }
        throw std::runtime_error{ "the board issued no request for a shelf it laid out" };
    }

    HomeShelfState StateOf(HomeShelfBoard const& board, std::wstring const& key)
    {
        auto const index = board.IndexOf(key);
        Require(index.has_value(), "a laid-out shelf lost its slot");
        return board.Slots()[*index].State;
    }

    std::vector<std::wstring> SlotKeys(HomeShelfBoard const& board)
    {
        std::vector<std::wstring> keys;
        for (auto const& slot : board.Slots())
        {
            keys.push_back(slot.Key);
        }
        return keys;
    }

    void TestShelfLoadingToLoadedEmptyFailed()
    {
        HomeShelfBoard board;
        Require(!board.ListKnown() && board.Slots().empty(), "a new board claimed a shelf list");

        auto const tickets = board.Plan(Keys({ L"a", L"b", L"c" }), HomeShelfRefetch::RefetchAll);
        Require(board.ListKnown(), "the shelf list was not known once laid out");
        Require(tickets.size() == 3, "every new shelf did not get its own request");
        for (auto const& slot : board.Slots())
        {
            Require(slot.State == HomeShelfState::Loading && slot.Fetching, "a new shelf did not start loading");
        }
        Require(!board.Settled(), "a board with requests out claimed to be settled");

        Require(board.Complete(TicketFor(tickets, L"a"), HomeShelfAnswer::Items) == HomeShelfApply::Applied,
            "a shelf's answer was not applied");
        Require(StateOf(board, L"a") == HomeShelfState::Loaded, "a shelf with titles did not become loaded");
        Require(StateOf(board, L"b") == HomeShelfState::Loading, "one shelf's answer settled another");
        Require(board.Complete(TicketFor(tickets, L"b"), HomeShelfAnswer::Empty) == HomeShelfApply::Applied,
            "an empty answer was not applied");
        Require(StateOf(board, L"b") == HomeShelfState::Empty, "an empty catalog did not become an empty shelf");
        Require(board.Complete(TicketFor(tickets, L"c"), HomeShelfAnswer::Failed) == HomeShelfApply::Applied,
            "a failure with nothing shown was not applied");
        Require(StateOf(board, L"c") == HomeShelfState::Failed, "a failed catalog did not become a failed shelf");
        Require(board.Settled() && !board.EveryShelfFailed(), "a partly failed load was reported as failed");

        Require(board.Complete(TicketFor(tickets, L"a"), HomeShelfAnswer::Empty) == HomeShelfApply::Discarded,
            "a request answered twice was applied twice");
        Require(StateOf(board, L"a") == HomeShelfState::Loaded, "a repeated answer changed a settled shelf");

        HomeShelfBoard failing;
        auto const failed = failing.Plan(Keys({ L"x", L"y" }), HomeShelfRefetch::RefetchAll);
        static_cast<void>(failing.Complete(failed[0], HomeShelfAnswer::Failed));
        Require(!failing.EveryShelfFailed(), "a load still in flight was reported as failed");
        static_cast<void>(failing.Complete(failed[1], HomeShelfAnswer::Failed));
        Require(failing.EveryShelfFailed(), "a load whose every catalog failed was not reported");

        HomeShelfBoard none;
        Require(none.Plan({}, HomeShelfRefetch::RefetchAll).empty(), "an empty shelf list asked for something");
        Require(none.ListKnown() && none.Settled() && !none.EveryShelfFailed(),
            "an empty shelf list was not a settled, successful one");
    }

    void TestShelfDuplicateKeys()
    {
        HomeShelfBoard board;
        auto const tickets = board.Plan(Keys({ L"a", L"a", L"b" }), HomeShelfRefetch::RefetchAll);
        Require(tickets.size() == 2 && board.Slots().size() == 2, "a repeated request became two shelves");
        Require(SlotKeys(board) == Keys({ L"a", L"b" }), "a repeated request did not keep its first place");
    }

    std::vector<std::wstring> ApplyEdits(
        std::vector<HomeRowIdentity> const& current,
        std::vector<HomeRowIdentity> const& next,
        std::size_t* replaced = nullptr,
        std::size_t* moved = nullptr)
    {
        std::vector<std::wstring> rows;
        for (auto const& row : current)
        {
            rows.push_back(row.Key + L"=" + row.Signature);
        }
        std::size_t replaces{};
        std::size_t structural{};
        for (auto const& edit : HaloDesktop::Services::PlanHomeRowEdits(current, next))
        {
            Require(edit.Index <= rows.size(), "a row edit pointed past the list");
            switch (edit.Kind)
            {
            case HomeRowEditKind::Remove:
                Require(edit.Index < rows.size(), "a row removal pointed past the list");
                rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(edit.Index));
                ++structural;
                break;
            case HomeRowEditKind::Insert:
                rows.insert(rows.begin() + static_cast<std::ptrdiff_t>(edit.Index), next[edit.Source].Key + L"=" + next[edit.Source].Signature);
                ++structural;
                break;
            case HomeRowEditKind::Replace:
                Require(edit.Index < rows.size(), "a row replacement pointed past the list");
                rows[edit.Index] = next[edit.Source].Key + L"=" + next[edit.Source].Signature;
                ++replaces;
                break;
            }
        }
        if (replaced) *replaced = replaces;
        if (moved) *moved = structural;
        return rows;
    }

    std::vector<std::wstring> Flatten(std::vector<HomeRowIdentity> const& rows)
    {
        std::vector<std::wstring> flat;
        for (auto const& row : rows)
        {
            flat.push_back(row.Key + L"=" + row.Signature);
        }
        return flat;
    }

    void TestShelfSlotStability()
    {
        HomeShelfBoard board;
        auto const tickets = board.Plan(Keys({ L"a", L"b", L"c", L"d" }), HomeShelfRefetch::RefetchAll);
        // Answers arrive in any order; the slots do not follow them.
        static_cast<void>(board.Complete(TicketFor(tickets, L"d"), HomeShelfAnswer::Items));
        static_cast<void>(board.Complete(TicketFor(tickets, L"b"), HomeShelfAnswer::Failed));
        static_cast<void>(board.Complete(TicketFor(tickets, L"a"), HomeShelfAnswer::Items));
        Require(SlotKeys(board) == Keys({ L"a", L"b", L"c", L"d" }), "a shelf moved when another answered");

        // What Home draws: the rows above the catalogs, then the shelves that
        // are loading or loaded. A shelf landing replaces only itself.
        std::vector<HomeRowIdentity> const loading{
            { L"recent", L"r1" }, { L"a", L"loading" }, { L"b", L"loading" }, { L"c", L"loading" }, { L"d", L"loading" } };
        std::vector<HomeRowIdentity> const landed{
            { L"recent", L"r1" }, { L"a", L"loading" }, { L"b", L"loading" }, { L"c", L"loading" }, { L"d", L"titles" } };
        std::size_t replaced{};
        std::size_t moved{};
        Require(ApplyEdits(loading, landed, &replaced, &moved) == Flatten(landed), "the edited rows were not the new rows");
        Require(replaced == 1 && moved == 0, "a shelf landing touched rows other than its own");

        // A failed shelf collapses: one removal, everything else stays put.
        std::vector<HomeRowIdentity> const collapsed{
            { L"recent", L"r1" }, { L"a", L"loading" }, { L"c", L"loading" }, { L"d", L"titles" } };
        Require(ApplyEdits(landed, collapsed, &replaced, &moved) == Flatten(collapsed), "a collapsed shelf left the rows wrong");
        Require(replaced == 0 && moved == 1, "a collapsed shelf moved or redrew other rows");

        // A row appearing above the catalogs goes in at its own place.
        std::vector<HomeRowIdentity> const withLibrary{
            { L"recent", L"r1" }, { L"library", L"l1" }, { L"a", L"loading" }, { L"c", L"loading" }, { L"d", L"titles" } };
        Require(ApplyEdits(collapsed, withLibrary, &replaced, &moved) == Flatten(withLibrary), "an inserted row landed in the wrong place");
        Require(replaced == 0 && moved == 1, "an inserted row disturbed the rows around it");

        // Nothing changed: nothing is edited.
        Require(HaloDesktop::Services::PlanHomeRowEdits(withLibrary, withLibrary).empty(), "an unchanged list was edited");
        // Whatever the change, the edits reach the new list exactly.
        std::vector<HomeRowIdentity> const reordered{ { L"d", L"titles" }, { L"x", L"new" }, { L"a", L"titles" } };
        Require(ApplyEdits(withLibrary, reordered) == Flatten(reordered), "a reordered list was not reached");
        Require(ApplyEdits({}, reordered) == Flatten(reordered), "an empty list did not fill");
        Require(ApplyEdits(reordered, {}).empty(), "a list did not empty");
    }

    void TestShelfAccountChangeMidFlight()
    {
        HomeShelfBoard board;
        auto const before = board.Plan(Keys({ L"a", L"b" }), HomeShelfRefetch::RefetchAll);
        static_cast<void>(board.Complete(TicketFor(before, L"a"), HomeShelfAnswer::Items));

        board.Reset();
        Require(!board.ListKnown() && board.Slots().empty(), "an account change kept the old account's shelves");
        Require(board.Complete(TicketFor(before, L"b"), HomeShelfAnswer::Items) == HomeShelfApply::Discarded,
            "an old account's answer landed after the account changed");

        // The new account lays out the same catalogs: the old requests still
        // cannot land in them.
        auto const after = board.Plan(Keys({ L"a", L"b" }), HomeShelfRefetch::RefetchAll);
        Require(board.Complete(TicketFor(before, L"b"), HomeShelfAnswer::Items) == HomeShelfApply::Discarded,
            "an old account's answer landed in the new account's shelf");
        Require(StateOf(board, L"b") == HomeShelfState::Loading, "the new account's shelf took the old account's titles");
        Require(StateOf(board, L"a") == HomeShelfState::Loading, "the new account started with the old account's titles");
        Require(board.Complete(TicketFor(after, L"b"), HomeShelfAnswer::Items) == HomeShelfApply::Applied,
            "the new account's own answer was refused");
    }

    void TestShelfRefreshWithoutSkeletonFlash()
    {
        HomeShelfBoard board;
        auto const first = board.Plan(Keys({ L"a", L"b", L"c" }), HomeShelfRefetch::RefetchAll);
        static_cast<void>(board.Complete(TicketFor(first, L"a"), HomeShelfAnswer::Items));
        static_cast<void>(board.Complete(TicketFor(first, L"b"), HomeShelfAnswer::Empty));
        static_cast<void>(board.Complete(TicketFor(first, L"c"), HomeShelfAnswer::Failed));

        // Addons changed: everything is asked again, and a new catalog joins.
        auto const second = board.Plan(Keys({ L"a", L"b", L"c", L"d" }), HomeShelfRefetch::RefetchAll);
        Require(second.size() == 4, "a refresh did not ask every shelf again");
        Require(StateOf(board, L"a") == HomeShelfState::Loaded, "a shown shelf fell back to skeletons for a refresh");
        Require(!HaloDesktop::Services::HomeShelfPending(board.Slots()[0]), "a shown shelf held the hero back during a refresh");
        Require(StateOf(board, L"b") == HomeShelfState::Empty, "an empty shelf reappeared as skeletons for a refresh");
        Require(StateOf(board, L"c") == HomeShelfState::Loading, "a failed shelf with nothing shown did not load again");
        Require(StateOf(board, L"d") == HomeShelfState::Loading, "a new catalog did not start loading");

        // A refetch that fails keeps what was shown.
        Require(board.Complete(TicketFor(second, L"a"), HomeShelfAnswer::Failed) == HomeShelfApply::KeptPrevious,
            "a failed refetch did not keep the shown titles");
        Require(StateOf(board, L"a") == HomeShelfState::Loaded, "a failed refetch hid a shown shelf");
        static_cast<void>(board.Complete(TicketFor(second, L"b"), HomeShelfAnswer::Failed));
        static_cast<void>(board.Complete(TicketFor(second, L"c"), HomeShelfAnswer::Failed));
        static_cast<void>(board.Complete(TicketFor(second, L"d"), HomeShelfAnswer::Failed));
        Require(board.EveryShelfFailed(), "a refresh whose every catalog failed was not reported");
        Require(StateOf(board, L"a") == HomeShelfState::Loaded, "an all-failed refresh dropped the shown titles");

        auto const third = board.Plan(Keys({ L"a", L"b", L"c", L"d" }), HomeShelfRefetch::RefetchAll);
        static_cast<void>(board.Complete(TicketFor(third, L"a"), HomeShelfAnswer::Items));
        Require(!board.EveryShelfFailed(), "a recovering refresh was still reported as failed");
    }

    void TestShelfStaleAnswerDiscarded()
    {
        HomeShelfBoard board;
        auto const older = board.Plan(Keys({ L"a" }), HomeShelfRefetch::RefetchAll);
        // A newer request for the same shelf supersedes the one in flight.
        auto const newer = board.Plan(Keys({ L"a" }), HomeShelfRefetch::RefetchAll);
        Require(newer.size() == 1 && newer[0].Generation != older[0].Generation, "a refetch reused the older request");
        Require(board.Complete(newer[0], HomeShelfAnswer::Empty) == HomeShelfApply::Applied, "the newer answer was refused");
        Require(board.Complete(older[0], HomeShelfAnswer::Items) == HomeShelfApply::Discarded,
            "an older answer overwrote a newer one");
        Require(StateOf(board, L"a") == HomeShelfState::Empty, "an older answer changed the shelf");

        // A language-only refresh: an unchanged request in flight goes on and
        // is not asked twice; a request whose language changed is a new shelf
        // in the same place, and the old language's answer cannot reach it.
        HomeShelfBoard language;
        auto const english = language.Plan(Keys({ L"top", L"films|genre=English" }), HomeShelfRefetch::RefetchAll);
        static_cast<void>(language.Complete(TicketFor(english, L"films|genre=English"), HomeShelfAnswer::Items));
        auto const french = language.Plan(Keys({ L"top", L"films|genre=French" }), HomeShelfRefetch::KeepAnswered);
        Require(french.size() == 1 && french[0].Key == L"films|genre=French", "a language change asked unchanged shelves again");
        Require(SlotKeys(language) == Keys({ L"top", L"films|genre=French" }), "a language change moved the shelf");
        Require(StateOf(language, L"films|genre=French") == HomeShelfState::Loading,
            "a shelf in a new language showed the old language's titles");
        Require(language.Complete(TicketFor(english, L"films|genre=English"), HomeShelfAnswer::Items) == HomeShelfApply::Discarded,
            "the old language's answer landed after the change");
        Require(language.Complete(TicketFor(english, L"top"), HomeShelfAnswer::Items) == HomeShelfApply::Applied,
            "an unchanged request in flight was lost to a language change");
    }

    FeaturedCandidate Candidate(std::wstring key, bool backdrop, HomeMediaKind kind = HomeMediaKind::Movie)
    {
        return { std::move(key), kind, backdrop };
    }

    HaloDesktop::Services::FeaturedPoolSlot Answered(std::vector<FeaturedCandidate> candidates)
    {
        return { false, std::move(candidates) };
    }

    HaloDesktop::Services::FeaturedPoolSlot Pending()
    {
        return { true, {} };
    }

    void TestHeroSettleRule()
    {
        using HaloDesktop::Services::SelectSettledFeatured;
        std::vector<FeaturedCandidate> const fiveBackdrops{
            Candidate(L"m:1", true), Candidate(L"m:2", true), Candidate(L"m:3", true),
            Candidate(L"m:4", true), Candidate(L"m:5", true) };

        Require(!SelectSettledFeatured(HomeFilter::All, std::vector{ Pending() }, 5).Settled,
            "the hero settled before any catalog answered");

        // Five backdrops from the first shelf: nothing later can displace them.
        auto const early = SelectSettledFeatured(HomeFilter::All, std::vector{ Answered(fiveBackdrops), Pending() }, 5);
        Require(early.Settled && early.Picks == std::vector<std::size_t>{ 0, 1, 2, 3, 4 },
            "five backdrops from the answered shelves did not settle the hero");

        // Five picks, one without a backdrop: a later backdrop would take its place.
        auto const mixed = SelectSettledFeatured(HomeFilter::All, std::vector{
            Answered({ Candidate(L"m:1", true), Candidate(L"m:2", true), Candidate(L"m:3", true), Candidate(L"m:4", true), Candidate(L"m:5", false) }),
            Pending() }, 5);
        Require(!mixed.Settled && mixed.Picks.empty(), "the hero settled on a pick a later backdrop could displace");

        Require(!SelectSettledFeatured(HomeFilter::All, std::vector{
            Answered({ Candidate(L"m:1", true), Candidate(L"m:2", true) }), Pending() }, 5).Settled,
            "too few picks settled the hero while a catalog was still out");

        // Every shelf answered, a failed one with nothing: the hero shows what there is.
        auto const all = SelectSettledFeatured(HomeFilter::All, std::vector{
            Answered({ Candidate(L"m:1", true), Candidate(L"m:2", false) }), Answered({}) }, 5);
        Require(all.Settled && all.Picks == std::vector<std::size_t>{ 0, 1 },
            "an answered Home did not settle on what it had");
        auto const nothing = SelectSettledFeatured(HomeFilter::All, std::vector{ Answered({}), Answered({}) }, 5);
        Require(nothing.Settled && nothing.Picks.empty(), "a Home with no titles left the hero waiting");

        // Only the answered prefix counts: a later shelf's backdrops cannot
        // settle the hero while an earlier one is still out.
        Require(!SelectSettledFeatured(HomeFilter::All, std::vector{
            Answered({ Candidate(L"m:1", true) }), Pending(), Answered(fiveBackdrops) }, 5).Settled,
            "the hero read past a shelf that had not answered");

        // Picks index the answered shelves laid end to end, and follow the filter.
        std::vector<FeaturedCandidate> series;
        for (auto const* key : { L"s:1", L"s:2", L"s:3", L"s:4", L"s:5" })
        {
            series.push_back(Candidate(key, true, HomeMediaKind::Series));
        }
        auto const pool = std::vector{ Answered({ Candidate(L"m:9", true) }), Answered(series), Pending() };
        auto const seriesOnly = SelectSettledFeatured(HomeFilter::Series, pool, 5);
        Require(seriesOnly.Settled && seriesOnly.Picks == std::vector<std::size_t>{ 1, 2, 3, 4, 5 },
            "the filter's picks did not settle the hero or were indexed wrongly");
        Require(!SelectSettledFeatured(HomeFilter::Movies, pool, 5).Settled,
            "one film settled a films-only hero while a catalog was still out");
    }
}

void RunHomeShelfLoadTests()
{
    TestShelfLoadingToLoadedEmptyFailed();
    TestShelfDuplicateKeys();
    TestShelfSlotStability();
    TestShelfAccountChangeMidFlight();
    TestShelfRefreshWithoutSkeletonFlash();
    TestShelfStaleAnswerDiscarded();
    TestHeroSettleRule();
}
