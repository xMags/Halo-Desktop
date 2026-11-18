#pragma once

#include "Services/Downloads/DownloadTypes.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

// The reference client's `downloads/downloadsLogic.ts`: the rules the
// Downloads page and the download service share about a transfer's state,
// sizes and timing, the throughput window, the bulk pause, and how a late
// list read is merged with newer engine events. Pure, so the tests include it
// by path.
namespace HaloDesktop::Services::DownloadsLogic
{
    using Downloads::DownloadFailureCode;
    using Downloads::DownloadStatus;

    // Failures a retry of the same request cannot fix; none is not one of them.
    [[nodiscard]] bool RequiresNewSource(std::optional<DownloadFailureCode> failure) noexcept;

    // Zero to one, or empty while the size is unknown.
    [[nodiscard]] std::optional<double> DownloadProgress(std::uint64_t totalBytes, std::uint64_t downloadedBytes) noexcept;

    [[nodiscard]] std::wstring DownloadStatusLabel(DownloadStatus status, std::optional<DownloadFailureCode> failure);

    // The short sentence other screens print for a failure; empty for none.
    // The Downloads page prints the engine's own sentences instead
    // (`Downloads::FailureMessage`).
    [[nodiscard]] std::optional<std::wstring> DownloadFailureMessage(std::optional<DownloadFailureCode> failure);

    // Whole seconds left at the current rate, rounded up; empty when the size
    // or the rate is unknown or nothing is left.
    [[nodiscard]] std::optional<std::uint64_t> DownloadEtaSeconds(
        std::uint64_t totalBytes,
        std::uint64_t downloadedBytes,
        std::uint64_t bytesPerSecond) noexcept;

    // `45s left`, `5m left`, `1h 1m left`.
    [[nodiscard]] std::wstring FormatDownloadEta(std::uint64_t seconds);

    // One decimal, ties rounded up as JavaScript's toFixed(1) does, which the
    // C runtime's printf (ties to even) does not: 3.25 reads 3.3.
    [[nodiscard]] std::wstring FormatOneDecimal(double value);

    // Binary units with one decimal (`1.0 KB`), whole bytes below a kilobyte,
    // and `0 B` for nothing.
    [[nodiscard]] std::wstring FormatDownloadBytes(double value);

    // The throughput chart's bars, which together cover one rolling minute,
    // sampled once per bar so the window always spans the same minute.
    inline constexpr std::size_t ThroughputSlots = 28;
    inline constexpr std::chrono::milliseconds ThroughputWindow{ 60'000 };
    // 60000 / 28 ms, kept exact in 100 ns ticks (the dispatcher timer's unit).
    inline constexpr std::chrono::duration<std::int64_t, std::ratio<1, 10'000'000>> ThroughputSampleInterval{
        std::chrono::duration_cast<std::chrono::duration<std::int64_t, std::ratio<1, 10'000'000>>>(ThroughputWindow).count()
        / static_cast<std::int64_t>(ThroughputSlots) };

    // Appends one sample, dropping the oldest once the window is full. A
    // negative or non-finite reading is stored as zero.
    void PushThroughputSample(std::vector<double>& samples, double value);

    // One record as the bulk pause sees it.
    struct PausableRecord final
    {
        std::wstring JobId;
        DownloadStatus Status{ DownloadStatus::Queued };
    };

    using JobIdSet = std::set<std::wstring, std::less<>>;

    // Paused as a page: nothing is moving and something the bulk pause stopped
    // is waiting to be told to move again. Queued counts as moving.
    [[nodiscard]] bool IsPausedAll(std::span<PausableRecord const> records, JobIdSet const& pausedByBulk);

    struct PauseAllPlan final
    {
        std::vector<std::wstring> Pause;
        // What "Resume all" may restart later: what this pause stops, plus what
        // an earlier bulk pause stopped that is still paused.
        JobIdSet Remembered;
    };

    [[nodiscard]] PauseAllPlan PlanPauseAll(std::span<PausableRecord const> records, JobIdSet const& pausedByBulk);

    // Only the transfers the bulk pause stopped, in record order.
    [[nodiscard]] std::vector<std::wstring> PlanResumeAll(std::span<PausableRecord const> records, JobIdSet const& pausedByBulk);

    // The fastest line reading kept, in megabits per second.
    inline constexpr double MaxLineMbps = 100'000.0;

    // A clearly faster line reading (more than 10% over the kept one).
    [[nodiscard]] bool IsNewLinePeak(double sampleMbps, double currentMbps) noexcept;

    // A list read merged with the engine events that arrived while it ran. For
    // any job an event touched after `since`, the current records (which
    // applied that event) win, including the job's absence; the list answers
    // for every other job. Ordered by creation, then id, like the index.
    // `Record` needs `JobId` (std::wstring) and `CreatedAt` members.
    template <typename Record>
    [[nodiscard]] std::vector<Record> MergeListRead(
        std::vector<Record> listed,
        std::vector<Record> const& current,
        std::unordered_map<std::wstring, std::uint64_t> const& lastEventSeq,
        std::uint64_t since)
    {
        auto const newer = [&lastEventSeq, since](std::wstring const& jobId)
        {
            auto const found = lastEventSeq.find(jobId);
            return found != lastEventSeq.end() && found->second > since;
        };
        std::vector<Record> merged;
        merged.reserve(listed.size() + current.size());
        for (auto& item : listed)
        {
            if (!newer(item.JobId)) merged.push_back(std::move(item));
        }
        for (auto const& item : current)
        {
            if (newer(item.JobId)) merged.push_back(item);
        }
        std::stable_sort(merged.begin(), merged.end(), [](Record const& left, Record const& right)
        {
            return std::tie(left.CreatedAt, left.JobId) < std::tie(right.CreatedAt, right.JobId);
        });
        return merged;
    }
}
