#pragma once

#include "Playback/IPlaybackEngine.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace HaloDesktop::Playback
{
    enum class SubtitleIntentKind
    {
        Automatic,
        Addon,
        Embedded,
        Off,
    };

    enum class SubtitleIntentResolution
    {
        GlobalPreference,
        Disable,
        ExactOnly,
        ExactThenLanguage,
        LanguageFallback,
    };

    struct PlaybackTimeline final
    {
        double PositionSeconds{};
        double DurationSeconds{};
    };

    [[nodiscard]] std::wstring NormalizeLanguage(std::wstring value);
    [[nodiscard]] std::wstring LanguageDisplayName(std::wstring const& code);
    [[nodiscard]] bool LanguageMatches(std::wstring const& left, std::wstring const& right);
    [[nodiscard]] std::optional<std::int64_t> FindLanguageTrack(
        std::vector<TrackInfo> const& tracks,
        TrackType type,
        std::wstring const& language,
        bool embeddedOnly);
    [[nodiscard]] std::wstring TrackSummary(std::vector<TrackInfo> const& tracks, TrackType type);
    [[nodiscard]] std::wstring EncodeExternalSubtitleTrackTitle(
        std::wstring const& identity,
        std::wstring const& displayTitle);
    [[nodiscard]] std::optional<std::pair<std::wstring,std::wstring>> DecodeExternalSubtitleTrackTitle(
        std::wstring const& encoded);
    [[nodiscard]] bool CanApplyAutomaticSelection(
        std::uint64_t currentSelectionSerial,
        std::uint64_t initialSelectionSerial,
        std::uint64_t automaticSelectionSerial) noexcept;
    [[nodiscard]] bool ShouldApplyResume(
        bool resumeEnabled,
        bool watched,
        double positionSeconds,
        double durationSeconds,
        double currentPositionSeconds,
        std::uint64_t currentSeekSerial,
        std::uint64_t initialSeekSerial,
        bool withinStartupWindow) noexcept;
    [[nodiscard]] bool ResolveBufferingState(
        bool current,
        std::optional<bool> pausedForCache,
        bool playbackReady,
        bool pausedForCacheActive) noexcept;
    // The player reports one stall for two causes: the stream opening or starving,
    // and a seek whose position is not live yet. A paused player is never stalled, so
    // the indicator can never land on top of the paused chip.
    [[nodiscard]] bool IsPlaybackStalled(
        bool buffering,
        bool seekPending,
        bool paused) noexcept;
    // How long a stall must last before it earns an indicator. A cached seek resolves
    // in tens of milliseconds and a marginal connection makes the cache flag flap, so
    // reporting either immediately reads as a rendering glitch. Opening a file is
    // exempt: the video surface is still black, so there is nothing to flicker against.
    [[nodiscard]] std::chrono::milliseconds BufferingIndicatorDelay(bool firstFrameReady) noexcept;
    // How much longer a visible indicator has to stay up. Zero once it has been shown
    // long enough to be dismissed at once.
    [[nodiscard]] std::chrono::milliseconds BufferingIndicatorHoldRemaining(
        std::chrono::milliseconds shownFor) noexcept;
    [[nodiscard]] bool ShouldReportPlaybackChange(
        bool endChanged,
        bool wasPlaying,
        bool isPlaying) noexcept;
    [[nodiscard]] bool ShouldExitMpvEventLoop(
        bool stopping,
        bool shutdownEvent) noexcept;
    [[nodiscard]] PlaybackTimeline NormalizePlaybackTimeline(
        double positionSeconds,
        double durationSeconds) noexcept;
    [[nodiscard]] bool IsPlaybackSpeedSelected(double actual, double choice) noexcept;
    [[nodiscard]] std::int32_t AdjustPlaybackDelayMilliseconds(
        std::int32_t current,
        std::int32_t delta) noexcept;
    [[nodiscard]] std::wstring SubtitleTrackFingerprint(TrackInfo const& track);
    [[nodiscard]] std::optional<std::int64_t> FindEmbeddedSubtitleByFingerprint(
        std::vector<TrackInfo> const& tracks,
        std::wstring const& fingerprint);
    [[nodiscard]] SubtitleIntentResolution ResolveSubtitleIntent(
        SubtitleIntentKind intent,
        bool exactVideo) noexcept;
    [[nodiscard]] std::wstring SerializePlaybackHeaders(std::vector<PlaybackHeader> const& headers);
}
