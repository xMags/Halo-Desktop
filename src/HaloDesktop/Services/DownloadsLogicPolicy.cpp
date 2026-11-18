#include "Services/DownloadsLogicPolicy.h"

#include <array>
#include <cmath>

namespace HaloDesktop::Services::DownloadsLogic
{
    bool RequiresNewSource(std::optional<DownloadFailureCode> failure) noexcept
    {
        return failure && Downloads::RequiresNewSource(*failure);
    }

    std::optional<double> DownloadProgress(std::uint64_t totalBytes, std::uint64_t downloadedBytes) noexcept
    {
        if (totalBytes == 0) return std::nullopt;
        return (std::clamp)(static_cast<double>(downloadedBytes) / static_cast<double>(totalBytes), 0.0, 1.0);
    }

    std::wstring DownloadStatusLabel(DownloadStatus status, std::optional<DownloadFailureCode> failure)
    {
        switch (status)
        {
        case DownloadStatus::Queued: return L"Waiting";
        case DownloadStatus::Downloading: return L"Downloading";
        case DownloadStatus::Paused: return L"Paused";
        case DownloadStatus::Done: return L"Ready offline";
        case DownloadStatus::Failed: return RequiresNewSource(failure) ? L"Choose source again" : L"Failed";
        }
        return L"Failed";
    }

    std::optional<std::wstring> DownloadFailureMessage(std::optional<DownloadFailureCode> failure)
    {
        if (!failure) return std::nullopt;
        switch (*failure)
        {
        case DownloadFailureCode::SourceExpired: return L"This source expired. Choose a source again to continue.";
        case DownloadFailureCode::StorageFull: return L"The download drive ran out of free space.";
        case DownloadFailureCode::InvalidRange: return L"This source cannot safely resume the partial file.";
        case DownloadFailureCode::MissingFile: return L"The downloaded file is no longer available.";
        case DownloadFailureCode::Network: return L"The transfer stopped after repeated network failures.";
        case DownloadFailureCode::ServerUnavailable: return L"The source remained unavailable after several retries.";
        case DownloadFailureCode::SourceRejected: return L"The source refused this download.";
        case DownloadFailureCode::ProtectedRequestCorrupt: return L"Choose the source again to rebuild the protected request.";
        case DownloadFailureCode::Unknown: return L"The download could not be completed.";
        }
        return L"The download could not be completed.";
    }

    std::optional<std::uint64_t> DownloadEtaSeconds(
        std::uint64_t totalBytes,
        std::uint64_t downloadedBytes,
        std::uint64_t bytesPerSecond) noexcept
    {
        if (totalBytes == 0 || bytesPerSecond == 0 || downloadedBytes >= totalBytes) return std::nullopt;
        auto const remaining = totalBytes - downloadedBytes;
        return remaining / bytesPerSecond + (remaining % bytesPerSecond == 0 ? 0 : 1);
    }

    std::wstring FormatDownloadEta(std::uint64_t seconds)
    {
        if (seconds < 60) return std::to_wstring(seconds) + L"s left";
        auto const minutes = seconds / 60 + (seconds % 60 == 0 ? 0 : 1);
        if (minutes < 60) return std::to_wstring(minutes) + L"m left";
        return std::to_wstring(minutes / 60) + L"h " + std::to_wstring(minutes % 60) + L"m left";
    }

    std::wstring FormatOneDecimal(double value)
    {
        if (!std::isfinite(value) || value <= 0.0) return L"0.0";
        auto const tenths = static_cast<std::uint64_t>(std::floor(value * 10.0 + 0.5));
        return std::to_wstring(tenths / 10) + L"." + std::to_wstring(tenths % 10);
    }

    std::wstring FormatDownloadBytes(double value)
    {
        if (!std::isfinite(value) || value <= 0.0) return L"0 B";
        constexpr std::array<wchar_t const*, 5> units{ L"B", L"KB", L"MB", L"GB", L"TB" };
        auto const exponent = std::floor(std::log(value) / std::log(1024.0));
        auto const index = static_cast<std::size_t>((std::clamp)(exponent, 0.0, static_cast<double>(units.size() - 1)));
        auto const scaled = value / std::pow(1024.0, static_cast<double>(index));
        auto const number = index == 0
            ? std::to_wstring(static_cast<std::uint64_t>(std::floor(scaled + 0.5)))
            : FormatOneDecimal(scaled);
        return number + L" " + units[index];
    }

    void PushThroughputSample(std::vector<double>& samples, double value)
    {
        samples.push_back(std::isfinite(value) && value > 0.0 ? value : 0.0);
        if (samples.size() > ThroughputSlots)
        {
            samples.erase(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(samples.size() - ThroughputSlots));
        }
    }

    bool IsPausedAll(std::span<PausableRecord const> records, JobIdSet const& pausedByBulk)
    {
        if (pausedByBulk.empty()) return false;
        auto tracked = false;
        for (auto const& record : records)
        {
            if (Downloads::IsActive(record.Status)) return false;
            if (pausedByBulk.contains(record.JobId))
            {
                tracked = true;
                if (record.Status != DownloadStatus::Paused) return false;
            }
        }
        return tracked;
    }

    PauseAllPlan PlanPauseAll(std::span<PausableRecord const> records, JobIdSet const& pausedByBulk)
    {
        PauseAllPlan plan;
        for (auto const& record : records)
        {
            if (Downloads::IsActive(record.Status))
            {
                plan.Pause.push_back(record.JobId);
                plan.Remembered.insert(record.JobId);
            }
            else if (record.Status == DownloadStatus::Paused && pausedByBulk.contains(record.JobId))
            {
                plan.Remembered.insert(record.JobId);
            }
        }
        return plan;
    }

    std::vector<std::wstring> PlanResumeAll(std::span<PausableRecord const> records, JobIdSet const& pausedByBulk)
    {
        std::vector<std::wstring> result;
        for (auto const& record : records)
        {
            if (record.Status == DownloadStatus::Paused && pausedByBulk.contains(record.JobId))
            {
                result.push_back(record.JobId);
            }
        }
        return result;
    }

    bool IsNewLinePeak(double sampleMbps, double currentMbps) noexcept
    {
        if (!std::isfinite(sampleMbps) || sampleMbps <= 0.0) return false;
        return (std::min)(sampleMbps, MaxLineMbps) > currentMbps * 1.1;
    }
}
