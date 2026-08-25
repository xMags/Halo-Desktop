#pragma once

#include "Playback/IPlaybackEngine.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace HaloDesktop::Playback
{
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
    void ValidatePlaybackHeaders(std::vector<PlaybackHeader> const& headers);
    [[nodiscard]] std::wstring SerializePlaybackHeaders(std::vector<PlaybackHeader> const& headers);
}
