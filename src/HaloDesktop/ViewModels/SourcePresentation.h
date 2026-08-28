#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <winrt/HaloDesktop.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace HaloDesktop::Sources
{
    // How the sheet talks about picture. Deliberately coarser than the parsed
    // resolution: the panel never says "2160p" to the user, and the filters and
    // the "best picture" sort both compare on this rather than on the raw string.
    enum class QualityTier
    {
        UltraHd,
        FullHd,
        Hd,
        Lower,
    };

    // The only thing a viewer actually has to decide between: can I press play
    // now, do I wait a moment, or does this have to come down first.
    enum class StartSpeed
    {
        Immediate,
        ShortWait,
        NeedsDownload,
    };

    // Everything the sheet knows about this device that a single source cannot
    // tell it. Assembled once per rebuild rather than per row, because reading
    // the preference file is a locked file read.
    struct DeviceContext final
    {
        std::optional<winrt::hstring> PreferredSubtitleLanguage;
        // Highest download rate this device has been measured at, megabits per
        // second. Zero means never measured, and suppresses the bitrate warning.
        double LineMbps{};
        // Runtime of the thing being played, seconds. Zero means unknown, and a
        // bitrate cannot be derived without it.
        double DurationSeconds{};
        // Position and duration of the viewer's progress, seconds. Both zero when
        // the title has never been opened.
        double WatchedSeconds{};
        double WatchedDurationSeconds{};
    };

    // A source flattened out of its addon group with everything the sheet sorts,
    // groups and filters on already decided, so no comparator ever re-parses a
    // display string.
    struct SourceEntry final
    {
        winrt::HaloDesktop::StreamSource Source{ nullptr };
        winrt::hstring Provider;
        QualityTier Tier{ QualityTier::Lower };
        StartSpeed Speed{ StartSpeed::NeedsDownload };
        bool Hdr{};
        bool Surround{};
        std::uint64_t SizeBytes{};
        std::int32_t Rank{};
        // Megabits per second this file needs to play without stalling. Zero when
        // either the size or the runtime is unknown.
        double NeededMbps{};
    };

    using SpecRow = std::pair<winrt::hstring, winrt::hstring>;

    [[nodiscard]] SourceEntry MakeEntry(
        winrt::HaloDesktop::StreamSource const& source,
        winrt::hstring const& provider,
        double durationSeconds);

    [[nodiscard]] QualityTier TierOf(winrt::hstring const& quality) noexcept;
    [[nodiscard]] winrt::hstring TierLabel(QualityTier tier);
    [[nodiscard]] StartSpeed SpeedOf(winrt::HaloDesktop::StreamStatus status) noexcept;
    [[nodiscard]] winrt::hstring StatusLabel(winrt::HaloDesktop::StreamStatus status);
    [[nodiscard]] winrt::hstring RangeLabel(winrt::hstring const& range);
    [[nodiscard]] bool IsHdr(winrt::hstring const& range) noexcept;
    [[nodiscard]] bool IsSurround(winrt::hstring const& audio) noexcept;
    [[nodiscard]] winrt::hstring SoundLabel(winrt::hstring const& audio);
    [[nodiscard]] winrt::hstring SizeLabel(winrt::hstring const& size);
    [[nodiscard]] winrt::hstring LanguageName(winrt::hstring const& code);
    [[nodiscard]] winrt::hstring AudioLanguageLine(winrt::hstring const& languages);
    [[nodiscard]] winrt::hstring SubtitleStatement(
        winrt::Windows::Foundation::Collections::IVectorView<winrt::hstring> const& tracks,
        std::optional<winrt::hstring> const& preferred);
    [[nodiscard]] winrt::hstring BitrateWarning(SourceEntry const& entry, double lineMbps);
    [[nodiscard]] winrt::hstring Ordinal(std::int32_t position);
    [[nodiscard]] winrt::hstring ReasonFor(
        SourceEntry const& entry,
        SourceEntry const* pick,
        bool isSmallest);
    // The single sentence under the quality plate on the recommended pick.
    [[nodiscard]] winrt::hstring PickHeadline(SourceEntry const& entry, bool alone);
    [[nodiscard]] winrt::hstring PickSummary(SourceEntry const& entry, DeviceContext const& device);
    [[nodiscard]] bool HasWatchProgress(DeviceContext const& device) noexcept;
    [[nodiscard]] winrt::hstring WatchNote(DeviceContext const& device);
    [[nodiscard]] std::vector<SpecRow> SpecsFor(
        SourceEntry const& entry,
        DeviceContext const& device);
    [[nodiscard]] winrt::hstring OutcomeGroupName(StartSpeed speed);
    [[nodiscard]] winrt::hstring OutcomeGroupNote(StartSpeed speed);
    [[nodiscard]] winrt::hstring CountLabel(std::size_t count, wchar_t const* singular, wchar_t const* plural);
}
