#include "SegmentPlannerTests.h"

#include "Services/Downloads/SegmentPlanner.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace
{
    namespace Segments = HaloDesktop::Services::Downloads::Segments;
    using Segments::ChooseWork;
    using Segments::Clock;
    using Segments::ConnectionBudget;
    using Segments::OverlapCheck;
    using Segments::PlanSegment;
    using Segments::PlanStep;
    using Segments::PlanStepKind;
    using Segments::SavedSegment;
    using Segments::SavedTable;
    using Segments::SegmentTable;

    constexpr std::uint64_t Mib = 1024ull * 1024ull;
    constexpr wchar_t const* V1 = L"\"v1\"";

    void Require(bool condition, char const* message)
    {
        if (!condition)
        {
            throw std::runtime_error{ message };
        }
    }

    [[nodiscard]] PlanSegment Plan(std::uint64_t start, std::uint64_t end, std::uint64_t position, bool assigned)
    {
        return PlanSegment{ .Start = start, .End = end, .Position = position, .Assigned = assigned };
    }

    [[nodiscard]] SavedTable Saved(std::uint64_t total, std::vector<std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>> const& segments)
    {
        SavedTable table{ .Version = Segments::TableVersion, .Total = total, .Validator = V1 };
        for (auto const& [start, end, done] : segments)
        {
            table.Segments.push_back(SavedSegment{ .Start = start, .End = end, .Done = done });
        }
        return table;
    }

    using Shape = std::vector<std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>>;

    [[nodiscard]] Shape ShapeOf(SegmentTable const& table)
    {
        Shape shape;
        for (auto const& segment : table.Segments())
        {
            shape.emplace_back(segment.Start, segment.End, segment.Position);
        }
        return shape;
    }

    [[nodiscard]] std::vector<std::uint8_t> Bytes(std::size_t count, std::uint8_t value)
    {
        return std::vector<std::uint8_t>(count, value);
    }

    void AnUnassignedSegmentWithTheMostLeftIsResumedFirst()
    {
        std::array const segments{
            Plan(0, 10 * Mib, 9 * Mib, false),
            Plan(10 * Mib, 20 * Mib, 10 * Mib, true),
            Plan(20 * Mib, 30 * Mib, 25 * Mib, false),
        };
        Require(ChooseWork(segments, Segments::MinSplitBytes) == PlanStep{ .Kind = PlanStepKind::Resume, .Index = 2 },
            "the unassigned segment with the most left was not resumed first");
    }

    void TiesGoToTheEarlierSegment()
    {
        std::array const segments{ Plan(0, 10, 5, false), Plan(10, 20, 15, false) };
        Require(ChooseWork(segments, 1) == PlanStep{ .Kind = PlanStepKind::Resume, .Index = 0 },
            "a tie did not go to the earlier segment");
    }

    void TheBusiestSegmentIsSplitAtTheMiddleOfWhatRemains()
    {
        std::array const segments{
            Plan(0, 10 * Mib, 2 * Mib, true),
            Plan(10 * Mib, 40 * Mib, 20 * Mib, true),
            Plan(40 * Mib, 50 * Mib, 50 * Mib, false),
        };
        Require(ChooseWork(segments, Segments::MinSplitBytes)
                == PlanStep{ .Kind = PlanStepKind::Split, .Index = 1, .At = 30 * Mib },
            "the busiest segment was not split at the middle of what remains");
    }

    void ASegmentBelowTheSplitThresholdIsLeftAlone()
    {
        std::array const below{ Plan(0, 100 * Mib, 100 * Mib - (Segments::MinSplitBytes - 1), true) };
        Require(!ChooseWork(below, Segments::MinSplitBytes), "a segment below the threshold was split");
        std::array const at{ Plan(0, 100 * Mib, 100 * Mib - Segments::MinSplitBytes, true) };
        Require(ChooseWork(at, Segments::MinSplitBytes)
                == PlanStep{ .Kind = PlanStepKind::Split, .Index = 0, .At = 100 * Mib - Segments::MinSplitBytes / 2 },
            "a segment at the threshold was not split");
    }

    void FinishedSegmentsGetNoWork()
    {
        std::array const segments{ Plan(0, 10, 10, false), Plan(10, 20, 20, true) };
        Require(!ChooseWork(segments, 1), "a finished segment got work");
    }

    void ASplitShortensTheBusySegmentAndStartsTheRestUnassigned()
    {
        auto table = SegmentTable::Contiguous(0, 32 * Mib);
        auto const first = table.Segments()[0].Key;
        table.Begin(first, 1);
        auto const now = Clock::now();
        auto const step = table.Choose(now, Segments::MinSplitBytes);
        Require(step.has_value(), "a busy segment offered no split");
        auto const added = table.TakeStep(*step);
        Require(added.has_value(), "the split did not create a segment");
        Require(table.Get(first)->End == 16 * Mib, "the busy segment was not shortened");
        auto const* second = table.Get(*added);
        Require(second->Start == 16 * Mib && second->End == 32 * Mib && second->Position == 16 * Mib,
            "the new segment does not cover the rest");
        Require(!second->Connection, "the new segment started assigned");
        Require(table.NextOf(first)->Key == *added, "the new segment does not follow the busy one");
        Require(table.PreviousOf(*added)->Key == first, "the busy segment does not precede the new one");
    }

    void ASegmentWaitingToRetryCountsAsTakenUntilItsTime()
    {
        auto table = SegmentTable::Contiguous(0, 32 * Mib);
        auto const now = Clock::now();
        auto const key = table.Segments()[0].Key;
        table.GetMutable(key)->RetryAt = now + std::chrono::seconds{ 1 };
        auto const early = table.Choose(now, Segments::MinSplitBytes);
        Require(early && early->Kind == PlanStepKind::Split, "a waiting segment was not counted as taken");
        Require(table.Choose(now + std::chrono::seconds{ 2 }, Segments::MinSplitBytes)
                == PlanStep{ .Kind = PlanStepKind::Resume, .Index = 0 },
            "a segment whose wait ended was not resumed");
        Require(table.NextRetry(now) == now + std::chrono::seconds{ 1 }, "the next retry is wrong");
    }

    void AnUnknownSizeIsNeverSplit()
    {
        auto table = SegmentTable::Contiguous(0, Segments::Unbounded);
        auto const key = table.Segments()[0].Key;
        table.Begin(key, 1);
        Require(!table.Choose(Clock::now(), 1), "a segment of unknown size was split");
    }

    void AnUnstartedSegmentIsGivenBackToTheOneBeforeIt()
    {
        auto table = SegmentTable::Contiguous(0, 32 * Mib);
        auto const first = table.Segments()[0].Key;
        table.Begin(first, 1);
        auto const added = table.Split(0, 16 * Mib);
        Require(added.has_value(), "the split failed");
        table.Begin(*added, 2);
        Require(!table.AbsorbNext(first), "a served segment was given back");
        table.Release(*added);
        Require(table.AbsorbNext(first), "an unstarted segment was not given back");
        Require(table.Segments().size() == 1, "the absorbed segment is still listed");
        Require(table.Get(first)->End == 32 * Mib, "the segment did not take the range back");
    }

    void ProgressIsTheSumOfEverySegmentsBytes()
    {
        auto table = SegmentTable::Contiguous(4, 100);
        auto const key = table.Segments()[1].Key;
        Require(table.Record(key, Bytes(10, 0)) == OverlapCheck::Consistent, "recording failed");
        auto const added = table.Split(1, 50);
        Require(added.has_value(), "the split failed");
        Require(table.Record(*added, Bytes(5, 0)) == OverlapCheck::Consistent, "recording failed");
        Require(table.Received() == 19, "progress is not the sum of every segment");
        Require(!table.IsComplete(), "an unfinished table reads as complete");
    }

    void TheSidecarRoundTripsAndResumesEachSegment()
    {
        auto table = SegmentTable::Contiguous(0, 100);
        auto const first = table.Segments()[0].Key;
        table.Begin(first, 1);
        Require(table.Record(first, Bytes(10, 1)) == OverlapCheck::Consistent, "recording failed");
        auto const second = table.Split(0, 60);
        Require(second.has_value(), "the split failed");
        Require(table.Record(*second, Bytes(5, 2)) == OverlapCheck::Consistent, "recording failed");
        auto const raw = Segments::Encode(table.Saved(100, V1));
        auto const decoded = Segments::Decode(raw);
        Require(decoded.has_value(), "the sidecar did not decode");
        Require(*decoded == Saved(100, { { 0, 60, 10 }, { 60, 100, 5 } }), "the sidecar did not round trip");
        auto const restored = SegmentTable::Restore(*decoded, 100, std::wstring_view{ V1 }, 100);
        Require(restored.has_value(), "the sidecar was not restored");
        Require(ShapeOf(*restored) == Shape{ { 0, 60, 10 }, { 60, 100, 65 } }, "a segment did not resume where it was");
        Require(std::all_of(restored->Segments().begin(), restored->Segments().end(),
                    [](Segments::Segment const& segment) { return !segment.Connection; }),
            "a restored segment came back assigned");
    }

    void FinishedNeighboursFoldIntoOneSegmentOnRestore()
    {
        auto const table = Saved(100, { { 0, 20, 20 }, { 20, 50, 30 }, { 50, 80, 7 }, { 80, 100, 0 } });
        auto const restored = SegmentTable::Restore(table, 100, std::wstring_view{ V1 }, 100);
        Require(restored.has_value(), "the sidecar was not restored");
        Require(ShapeOf(*restored) == Shape{ { 0, 80, 57 }, { 80, 100, 80 } }, "finished neighbours did not fold");
        Require(restored->Received() == 57, "folding changed the progress");
    }

    void ASidecarThatNoLongerFitsTheDownloadIsRefused()
    {
        auto good = Saved(100, { { 0, 50, 50 }, { 50, 100, 10 } });
        std::optional<std::wstring_view> const v1{ V1 };
        Require(SegmentTable::Restore(good, 100, v1, 100).has_value(), "a fitting sidecar was refused");
        // Another size, another validator, or none at all.
        Require(!SegmentTable::Restore(good, 101, v1, 100), "a sidecar of another size was restored");
        Require(!SegmentTable::Restore(good, 100, std::wstring_view{ L"\"v2\"" }, 100), "a sidecar of another validator was restored");
        Require(!SegmentTable::Restore(good, 100, std::nullopt, 100), "a sidecar without a validator was restored");
        // A claim to bytes past the end of the part, or a part longer than the file.
        Require(!SegmentTable::Restore(good, 100, v1, 59), "a claim past the part was restored");
        Require(!SegmentTable::Restore(good, 100, v1, 101), "a part longer than the file was restored");
        // Gaps, overlaps, a short cover, overstated progress, an unknown version.
        for (auto const& broken : {
                 Saved(100, { { 0, 40, 0 }, { 50, 100, 0 } }),
                 Saved(100, { { 0, 60, 0 }, { 50, 100, 0 } }),
                 Saved(100, { { 0, 50, 0 }, { 50, 90, 0 } }),
                 Saved(100, { { 0, 50, 51 }, { 50, 100, 0 } }),
                 Saved(100, { { 10, 100, 0 } }),
                 Saved(100, {}),
             })
        {
            Require(!SegmentTable::Restore(broken, 100, v1, 100), "a broken sidecar was restored");
        }
        auto future = good;
        future.Version = 2;
        Require(!SegmentTable::Restore(future, 100, v1, 100), "a sidecar of an unknown version was restored");
        Require(!Segments::Decode("{ not json"), "a torn sidecar decoded");
    }

    void APartFromBeforeSidecarsIsOneFinishedSegment()
    {
        auto const table = SegmentTable::Contiguous(40, 100);
        Require(ShapeOf(table) == Shape{ { 0, 40, 40 }, { 40, 100, 40 } }, "a legacy part is not one finished segment");
        Require(table.Received() == 40, "a legacy part's progress is wrong");
        auto const fresh = SegmentTable::Contiguous(0, 100);
        Require(fresh.Segments().size() == 1 && fresh.Received() == 0, "a fresh table is not one empty segment");
    }

    void MatchingOverlapBytesPassInEitherOrder()
    {
        {
            // The next segment's head arrives first.
            auto table = SegmentTable::Contiguous(0, 200);
            auto const first = table.Segments()[0].Key;
            auto const second = table.Split(0, 100);
            Require(second.has_value(), "the split failed");
            table.Begin(*second, 2);
            Require(table.Record(*second, Bytes(80, 7)) == OverlapCheck::Consistent, "recording failed");
            Require(table.OverlapWanted(first, 200) == Segments::OverlapBytes, "the overlap wanted is wrong");
            Require(table.HandOverOverlap(first, Bytes(Segments::OverlapBytes, 7)) == OverlapCheck::Consistent,
                "matching overlap bytes were refused");
        }
        {
            // The overlap arrives first and is compared as the head grows.
            auto table = SegmentTable::Contiguous(0, 200);
            auto const first = table.Segments()[0].Key;
            auto const second = table.Split(0, 100);
            Require(second.has_value(), "the split failed");
            table.Begin(*second, 2);
            Require(table.HandOverOverlap(first, Bytes(Segments::OverlapBytes, 7)) == OverlapCheck::Consistent,
                "an early overlap was refused");
            Require(table.Record(*second, Bytes(10, 7)) == OverlapCheck::Consistent, "a matching head was refused");
            Require(table.Record(*second, Bytes(70, 7)) == OverlapCheck::Consistent, "a matching head was refused");
        }
    }

    void DifferentOverlapBytesAreAMismatch()
    {
        {
            auto table = SegmentTable::Contiguous(0, 200);
            auto const first = table.Segments()[0].Key;
            auto const second = table.Split(0, 100);
            Require(second.has_value(), "the split failed");
            table.Begin(*second, 2);
            Require(table.Record(*second, Bytes(80, 7)) == OverlapCheck::Consistent, "recording failed");
            auto overlap = Bytes(Segments::OverlapBytes, 7);
            overlap[Segments::OverlapBytes - 1] = 8;
            Require(table.HandOverOverlap(first, overlap) == OverlapCheck::Mismatch, "a different overlap passed");
        }
        {
            auto table = SegmentTable::Contiguous(0, 200);
            auto const first = table.Segments()[0].Key;
            auto const second = table.Split(0, 100);
            Require(second.has_value(), "the split failed");
            table.Begin(*second, 2);
            Require(table.HandOverOverlap(first, Bytes(Segments::OverlapBytes, 7)) == OverlapCheck::Consistent,
                "an early overlap was refused");
            Require(table.Record(*second, Bytes(4, 9)) == OverlapCheck::Mismatch, "a different head passed");
        }
    }

    void TheOverlapStopsAtTheEndOfTheFileAndSkipsResumedSegments()
    {
        auto table = SegmentTable::Contiguous(0, 130);
        auto const first = table.Segments()[0].Key;
        Require(table.Split(0, 100).has_value(), "the split failed");
        Require(table.OverlapWanted(first, 130) == 30, "the overlap ran past the end of the file");
        auto const resumed = SegmentTable::Restore(
            Saved(130, { { 0, 100, 10 }, { 100, 130, 5 } }), 130, std::wstring_view{ V1 }, 130);
        Require(resumed.has_value(), "the sidecar was not restored");
        Require(resumed->OverlapWanted(resumed->Segments()[0].Key, 130) == 0, "a resumed segment was compared");
        Require(resumed->OverlapWanted(resumed->Segments()[1].Key, 130) == 0, "the last segment wanted an overlap");
    }

    void TheBudgetRampsOneAtATimeAndKeepsItsCeiling()
    {
        auto const start = Clock::now();
        ConnectionBudget budget{ Segments::MaxConnections, Segments::RampInterval, start };
        Require(budget.Allowed() == 1, "the budget did not start at one");
        budget.Ramp(start);
        Require(budget.Allowed() == 1, "the budget grew before the first interval");
        auto now = start;
        for (std::size_t expected = 2; expected <= Segments::MaxConnections; ++expected)
        {
            now += Segments::RampInterval;
            budget.Ramp(now);
            budget.Ramp(now);
            Require(budget.Allowed() == expected, "the budget did not grow by one per interval");
        }
        now += Segments::RampInterval;
        budget.Ramp(now);
        Require(budget.Allowed() == Segments::MaxConnections, "the budget grew past its maximum");
        Require(!budget.NextRamp(), "a full budget still plans to grow");
        budget.Refuse(3);
        Require(budget.Allowed() == 3, "a refusal did not set the ceiling");
        for (int index = 0; index < 10; ++index)
        {
            now += Segments::RampInterval;
            budget.Ramp(now);
        }
        Require(budget.Allowed() == 3, "the budget grew past its ceiling again");
        budget.Refuse(0);
        Require(budget.Allowed() == 1, "a refusal with nothing receiving did not keep one connection");
    }

    void RetryDelaysHonourRetryAfterAndOtherwiseBackOff()
    {
        using std::chrono::seconds;
        Require(Segments::RetryDelay(1, std::nullopt) == seconds{ 1 }, "the first retry delay is wrong");
        Require(Segments::RetryDelay(2, std::nullopt) == seconds{ 2 }, "the second retry delay is wrong");
        Require(Segments::RetryDelay(3, std::nullopt) == seconds{ 4 }, "the third retry delay is wrong");
        Require(Segments::RetryDelay(9, std::nullopt) == seconds{ 4 }, "the retry delay grew past four seconds");
        Require(Segments::RetryDelay(1, 7) == seconds{ 7 }, "Retry-After was not honoured");
        Require(Segments::RetryDelay(1, 3600) == Segments::MaxRetryAfter, "Retry-After was not capped");
    }

    void SidecarPathsSitBesideThePart()
    {
        auto const table = Segments::TablePath(LR"(C:\Halo\movie.mkv.part)");
        Require(table == std::filesystem::path{ LR"(C:\Halo\movie.mkv.part.segments)" }, "the sidecar path is wrong");
        Require(Segments::TableTempPath(table) == std::filesystem::path{ LR"(C:\Halo\movie.mkv.part.segments.tmp)" },
            "the sidecar temporary path is wrong");
    }

    // Not in the reference, which leans on serde: the hand-written reader must
    // keep a validator's quotes and anything outside ASCII, ignore fields it does
    // not know, and refuse what serde would refuse.
    void TheSidecarFormatIsStrictAndKeepsTheValidator()
    {
        SavedTable table{ .Version = 1, .Total = (std::numeric_limits<std::uint64_t>::max)(), .Validator = L"W/\"\u00e9t\\ag\"" };
        table.Segments.push_back(SavedSegment{ .Start = 0, .End = 5, .Done = 5 });
        auto const raw = Segments::Encode(table);
        Require(std::all_of(raw.begin(), raw.end(), [](char character) { return static_cast<unsigned char>(character) < 0x80; }),
            "the sidecar is not plain ASCII");
        Require(Segments::Decode(raw) == table, "a validator with quotes or non-ASCII text did not round trip");
        Require(Segments::Decode(R"({"version":1,"total":5,"extra":[1,{"a":null}],"validator":"x","segments":[{"start":0,"end":5,"done":0,"more":true}]})").has_value(),
            "an unknown field was not ignored");
        for (auto const* broken : {
                 R"({"version":1,"total":5,"validator":"x"})",
                 R"({"version":1,"version":1,"total":5,"validator":"x","segments":[]})",
                 R"({"version":1,"total":5.0,"validator":"x","segments":[]})",
                 R"({"version":1,"total":-5,"validator":"x","segments":[]})",
                 R"({"version":1,"total":05,"validator":"x","segments":[]})",
                 R"({"version":1,"total":18446744073709551616,"validator":"x","segments":[]})",
                 R"({"version":4294967296,"total":5,"validator":"x","segments":[]})",
                 R"({"version":1,"total":5,"validator":"x","segments":[]} trailing)",
                 R"({"version":1,"total":5,"validator":"x\q","segments":[]})",
                 "",
             })
        {
            Require(!Segments::Decode(broken), "a malformed sidecar decoded");
        }
    }
}

void RunSegmentPlannerTests()
{
    AnUnassignedSegmentWithTheMostLeftIsResumedFirst();
    TiesGoToTheEarlierSegment();
    TheBusiestSegmentIsSplitAtTheMiddleOfWhatRemains();
    ASegmentBelowTheSplitThresholdIsLeftAlone();
    FinishedSegmentsGetNoWork();
    ASplitShortensTheBusySegmentAndStartsTheRestUnassigned();
    ASegmentWaitingToRetryCountsAsTakenUntilItsTime();
    AnUnknownSizeIsNeverSplit();
    AnUnstartedSegmentIsGivenBackToTheOneBeforeIt();
    ProgressIsTheSumOfEverySegmentsBytes();
    TheSidecarRoundTripsAndResumesEachSegment();
    FinishedNeighboursFoldIntoOneSegmentOnRestore();
    ASidecarThatNoLongerFitsTheDownloadIsRefused();
    APartFromBeforeSidecarsIsOneFinishedSegment();
    MatchingOverlapBytesPassInEitherOrder();
    DifferentOverlapBytesAreAMismatch();
    TheOverlapStopsAtTheEndOfTheFileAndSkipsResumedSegments();
    TheBudgetRampsOneAtATimeAndKeepsItsCeiling();
    RetryDelaysHonourRetryAfterAndOtherwiseBackOff();
    SidecarPathsSitBesideThePart();
    TheSidecarFormatIsStrictAndKeepsTheValidator();
}
