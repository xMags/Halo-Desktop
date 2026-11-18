#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The segment table of one download: which byte ranges exist, how far each has
// arrived, which connection serves it, and the sidecar that carries the table
// from one run to the next. Pure bookkeeping with no network, no disk and no
// WinRT, so every rule here is tested on its own.
//
// The planning rules are Last Download Manager's: a free connection takes the
// unassigned segment with the most bytes left, and otherwise halves what
// remains of the busiest one.
namespace HaloDesktop::Services::Downloads::Segments
{
    using Clock = std::chrono::steady_clock;

    // The most connections one download opens at once.
    inline constexpr std::size_t MaxConnections = 8;
    // A segment is split only when this much remains, so both halves are worth a
    // connection. Each connection costs the source a request, which a debrid
    // service counts, so this is larger than a general download manager's.
    inline constexpr std::uint64_t MinSplitBytes = 4ull * 1024ull * 1024ull;
    // One more connection is allowed this often until the limit is reached.
    inline constexpr std::chrono::milliseconds RampInterval{ 250 };
    // Bytes a connection reads past its segment's end, compared with what the
    // next segment's own connection received there.
    inline constexpr std::size_t OverlapBytes = 64;
    // The end of a segment whose source never stated the file's size.
    inline constexpr std::uint64_t Unbounded = (std::numeric_limits<std::uint64_t>::max)();
    inline constexpr std::chrono::seconds MaxRetryAfter{ 60 };
    inline constexpr std::uint32_t TableVersion = 1;
    // A table is rebuilt from a file anyone can edit; this bounds what it costs.
    inline constexpr std::size_t MaxSavedSegments = 4096;

    // The connection strategy's tunable values. Tests shorten them.
    struct Tuning final
    {
        std::size_t MaxConnections{ Segments::MaxConnections };
        std::uint64_t MinSplitBytes{ Segments::MinSplitBytes };
        Clock::duration RampInterval{ Segments::RampInterval };
    };

    // The planner's view of one segment.
    struct PlanSegment final
    {
        std::uint64_t Start{};
        // Exclusive.
        std::uint64_t End{};
        // The next byte to receive.
        std::uint64_t Position{};
        // A connection serves it, or it waits to be retried.
        bool Assigned{};

        [[nodiscard]] std::uint64_t Remaining() const noexcept;
        friend bool operator==(PlanSegment const&, PlanSegment const&) = default;
    };

    enum class PlanStepKind
    {
        // Serve the unassigned segment at Index from its position.
        Resume,
        // End the segment at Index at At; the new connection takes the rest.
        Split,
    };

    // Work for one free connection.
    struct PlanStep final
    {
        PlanStepKind Kind{ PlanStepKind::Resume };
        std::size_t Index{};
        // Only meaningful for a split.
        std::uint64_t At{};

        friend bool operator==(PlanStep const&, PlanStep const&) = default;
    };

    // Chooses work for one newly available connection: an unassigned, incomplete
    // segment, largest remaining first; otherwise the segment with the most bytes
    // left, split at the middle of what remains when that is at least
    // minSplitBytes; otherwise nothing, because another connection would not
    // help. Ties go to the earlier segment.
    [[nodiscard]] std::optional<PlanStep> ChooseWork(
        std::span<PlanSegment const> segments,
        std::uint64_t minSplitBytes) noexcept;

    // How long a refused segment waits before it is asked for again: what the
    // source's Retry-After said, capped, or else 1, 2 and then 4 seconds.
    [[nodiscard]] std::chrono::milliseconds RetryDelay(
        std::uint32_t attempt,
        std::optional<std::uint64_t> retryAfterSeconds) noexcept;

    // How many connections a run may have open. It starts at one, grows by one
    // every ramp interval, and once the source refuses a connection while others
    // still receive, never grows past what it took at that moment again.
    class ConnectionBudget final
    {
    public:
        ConnectionBudget(std::size_t max, Clock::duration interval, Clock::time_point now) noexcept;

        void Ramp(Clock::time_point now) noexcept;
        [[nodiscard]] std::size_t Allowed() const noexcept;
        // The source refused a connection while `live` others kept receiving.
        void Refuse(std::size_t live) noexcept;
        // When the budget grows next, if it still can.
        [[nodiscard]] std::optional<Clock::time_point> NextRamp() const noexcept;

    private:
        [[nodiscard]] std::size_t Cap() const noexcept;

        std::size_t m_max;
        Clock::duration m_interval;
        std::size_t m_allowed{ 1 };
        std::size_t m_ceiling;
        Clock::time_point m_nextRamp;
    };

    // Two connections delivered different bytes for the same offsets: the source
    // served two different objects under one validator.
    enum class OverlapCheck
    {
        Consistent,
        Mismatch,
    };

    class SegmentTable;

    struct Segment final
    {
        std::uint32_t Key{};
        std::uint64_t Start{};
        // Exclusive; Unbounded while the file's size is unknown.
        std::uint64_t End{};
        // The next byte to receive; everything before it is written.
        std::uint64_t Position{};
        std::optional<std::uint64_t> Connection;
        std::optional<Clock::time_point> RetryAt;
        std::uint32_t Attempts{};

        [[nodiscard]] bool IsComplete() const noexcept;

    private:
        friend class SegmentTable;

        [[nodiscard]] OverlapCheck VerifyOverlap();

        // The first bytes of the segment as its own connection received them.
        std::vector<std::uint8_t> m_head;
        bool m_headFromStart{};
        // Bytes the previous segment's connection read past its end, not yet all
        // compared with m_head.
        std::vector<std::uint8_t> m_overlapFromPrevious;
    };

    // The table as the sidecar stores it. Done counts the bytes from Start that
    // were on disk, flushed, when the table was written.
    struct SavedSegment final
    {
        std::uint64_t Start{};
        std::uint64_t End{};
        std::uint64_t Done{};

        friend bool operator==(SavedSegment const&, SavedSegment const&) = default;
    };

    struct SavedTable final
    {
        std::uint32_t Version{};
        std::uint64_t Total{};
        std::wstring Validator;
        std::vector<SavedSegment> Segments;

        friend bool operator==(SavedTable const&, SavedTable const&) = default;
    };

    // The sidecar's JSON: {"version":1,"total":N,"validator":"...","segments":
    // [{"start":N,"end":N,"done":N},...]}. The output is plain ASCII: anything
    // else in the validator is written as a \u escape.
    [[nodiscard]] std::string Encode(SavedTable const& table);
    // Nothing for anything that is not that shape, so a torn or edited sidecar
    // means a fresh start rather than a guess.
    [[nodiscard]] std::optional<SavedTable> Decode(std::string_view raw);

    // The sidecar beside a part file.
    [[nodiscard]] std::filesystem::path TablePath(std::filesystem::path const& part);
    // Where a sidecar would be written before it replaces the old one.
    [[nodiscard]] std::filesystem::path TableTempPath(std::filesystem::path const& table);

    // Segments in file order, contiguous from 0 to the end of the file. Pointers
    // returned by Get and GetMutable are valid only until the next call that
    // splits, absorbs or rebuilds the table.
    class SegmentTable final
    {
    public:
        // The table of a part written front to back: `done` bytes received, the
        // rest one segment up to `end`.
        [[nodiscard]] static SegmentTable Contiguous(std::uint64_t done, std::uint64_t end);

        // The table a sidecar describes, when it still describes this download:
        // same size, same validator, segments that cover the file exactly, and no
        // claim to bytes past the end of the part on disk. Neighbours behind a
        // finished segment are folded into it so tables do not grow from run to
        // run.
        [[nodiscard]] static std::optional<SegmentTable> Restore(
            SavedTable const& saved,
            std::uint64_t total,
            std::optional<std::wstring_view> validator,
            std::uint64_t partLength);

        // The table as it stands, for the sidecar. The caller writes it only after
        // flushing the part, so every done byte is on disk.
        [[nodiscard]] SavedTable Saved(std::uint64_t total, std::wstring const& validator) const;

        [[nodiscard]] std::vector<Segment> const& Segments() const noexcept;
        [[nodiscard]] Segment const* Get(std::uint32_t key) const noexcept;
        [[nodiscard]] Segment* GetMutable(std::uint32_t key) noexcept;
        // The segment that starts where `key` ends.
        [[nodiscard]] Segment const* NextOf(std::uint32_t key) const noexcept;
        // The segment that ends where `key` starts.
        [[nodiscard]] Segment const* PreviousOf(std::uint32_t key) const noexcept;

        // Work for one free connection, with segments that wait to be retried
        // counted as taken. Nothing while the file's size is unknown.
        [[nodiscard]] std::optional<PlanStep> Choose(
            Clock::time_point now,
            std::uint64_t minSplitBytes) const;
        // The key of the segment a step serves, splitting first when it says so.
        [[nodiscard]] std::optional<std::uint32_t> TakeStep(PlanStep step);
        // Ends the segment at `index` at `at` and inserts the rest after it.
        [[nodiscard]] std::optional<std::uint32_t> Split(std::size_t index, std::uint64_t at);
        // Gives the segment after `key` back to it, when no connection serves that
        // one and nothing of it arrived yet. Returns whether it did.
        [[nodiscard]] bool AbsorbNext(std::uint32_t key);

        // `connection` starts serving `key` from its position. Starting at the
        // segment's first byte, it records the head the overlap check needs.
        void Begin(std::uint32_t key, std::uint64_t connection);
        // The connection serving `key` stopped.
        void Release(std::uint32_t key) noexcept;
        // `bytes` were written at `key`'s position.
        [[nodiscard]] OverlapCheck Record(std::uint32_t key, std::span<std::uint8_t const> bytes);

        // How many bytes past its end the connection serving `key` should read for
        // the overlap check: none when no next segment can still be compared (one
        // resumed from an earlier run has no head from its start).
        [[nodiscard]] std::size_t OverlapWanted(std::uint32_t key, std::uint64_t total) const noexcept;
        // Hands the bytes read past `key`'s end to the next segment, comparing what
        // its own connection already received.
        [[nodiscard]] OverlapCheck HandOverOverlap(std::uint32_t key, std::vector<std::uint8_t> overlap);

        // Bytes written across every segment.
        [[nodiscard]] std::uint64_t Received() const noexcept;
        [[nodiscard]] bool IsComplete() const noexcept;
        // The earliest moment a waiting segment may be asked for again.
        [[nodiscard]] std::optional<Clock::time_point> NextRetry(Clock::time_point now) const noexcept;

    private:
        void Push(std::uint64_t start, std::uint64_t end, std::uint64_t position);
        [[nodiscard]] std::optional<std::size_t> IndexOf(std::uint32_t key) const noexcept;

        std::vector<Segment> m_segments;
        std::uint32_t m_nextKey{};
    };
}
