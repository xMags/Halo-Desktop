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

    // The parts of a watch-history row that decide where a video opens.
    struct SavedPlace final
    {
        double PositionSeconds{};
        bool Watched{};
        bool Removed{};
    };

    // The reference client's `resumePosition`: exactly where the video was left,
    // whenever there is a place to go back to. Nothing for a missing, watched or
    // removed row, a position that is not above zero, or one at or past the end of
    // the file being opened (a shorter cut). An unknown length (0) does not stop it.
    [[nodiscard]] std::optional<double> ResumePosition(
        std::optional<SavedPlace> const& saved,
        double durationSeconds) noexcept;

    // Whether the resume may still move the playhead: the watch history can arrive
    // after the file has started, and by then the viewer may have seeked or watched
    // on, which a late resume must not undo.
    [[nodiscard]] bool CanApplyResumeNow(
        double currentPositionSeconds,
        std::uint64_t currentSeekSerial,
        std::uint64_t initialSeekSerial,
        bool withinStartupWindow) noexcept;

    // What a track is called in the player: its trimmed title, else the name of
    // its language, else "Track {id}".
    [[nodiscard]] std::wstring TrackDisplayTitle(
        std::wstring const& title,
        std::wstring const& language,
        std::int64_t id);

    // The reference's `subtitleFormat`: which appearance controls can change a
    // subtitle track, by its codec.
    enum class SubtitleFormat
    {
        Styled,
        Text,
        Bitmap,
        Unknown,
    };

    struct SubtitleControlAvailability final
    {
        bool TrackStyling{};
        bool Looks{};

        bool operator==(SubtitleControlAvailability const&) const = default;
    };

    [[nodiscard]] SubtitleFormat SubtitleFormatOf(std::wstring const& codec);
    // A picture track takes no font, outline or shadow; a plain text track has no
    // look of its own to keep; a styled track that keeps its look ignores font,
    // outline and shadow. An unknown codec leaves everything usable.
    [[nodiscard]] SubtitleControlAvailability SubtitleControlsFor(
        SubtitleFormat format,
        bool keepTrackStyling) noexcept;
    [[nodiscard]] std::wstring TrackStylingNote(SubtitleFormat format, bool keepTrackStyling);
    [[nodiscard]] std::wstring SubtitleSizeNote(SubtitleFormat format);
    // Opacity of an appearance control the selected track ignores.
    inline constexpr double DisabledSubtitleControlOpacity = 0.45;

    // The seek bar has a scale only once the file has a length.
    [[nodiscard]] bool IsSeekableDuration(double durationSeconds) noexcept;

    // What the end of a file leads to, in the reference player's order.
    enum class EndOfVideoAction
    {
        PlayNextDownload,
        ShowUpNext,
        OpenNextSources,
        ClosePlayer,
    };

    struct EndOfVideoContext final
    {
        bool Autoplay{};
        bool PlayingDownload{};
        bool NextDownloadReady{};
        bool NextHasStream{};
        bool NextEpisodeKnown{};
    };

    [[nodiscard]] EndOfVideoAction ResolveEndOfVideo(EndOfVideoContext const& context) noexcept;

    // Up Next's cancel at the real end of a file leaves the player, as the
    // reference's `upNextCancelAction`; before the end it only dismisses the card.
    enum class UpNextCancelAction
    {
        Dismiss,
        Exit,
    };

    [[nodiscard]] UpNextCancelAction ResolveUpNextCancel(bool endReached) noexcept;

    inline constexpr std::int32_t UpNextCountdownSeconds = 8;

    // The next episode's name in the player and the sources sheet: its own title
    // (the mapper has already fallen back to `name`), else the show's name, else
    // the title of what is playing. The SxxEyy tag travels separately.
    [[nodiscard]] std::wstring NextEpisodeTitle(
        std::wstring const& ownTitle,
        std::wstring const& showName,
        std::wstring const& currentTitle);

    // The framing a device preference asks for; fit unless fill was chosen.
    [[nodiscard]] VideoFitMode VideoFitForPreference(bool fillPicture) noexcept;

    // What `eof-reached` and `end-file` mean under keep-open. A file that plays
    // out pauses on its last frame and raises eof-reached; a seek back from there
    // clears it, which lets the end be handled again.
    enum class EndOfFileSignal
    {
        EofReached,
        EofCleared,
        EndFileEof,
        FileLoaded,
    };

    // Returns whether this signal is a new end to act on, and updates `handled`.
    [[nodiscard]] bool AcceptEndOfFileSignal(bool& handled, EndOfFileSignal signal) noexcept;
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
    // Whether playback at this place is worth a watch history row: never for
    // a request that keeps no history, nor while the video is under a minute
    // long or barely started.
    [[nodiscard]] bool ShouldKeepWatchRow(
        bool recordsHistory,
        double positionSeconds,
        double durationSeconds) noexcept;
    [[nodiscard]] bool ShouldReportPlaybackChange(
        bool endChanged,
        bool wasPaused,
        bool isPaused,
        PlaybackEndReason endReason) noexcept;
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
    // Clock face for a playback position. Hours are opt-in rather than inferred from the
    // value so that every timestamp for one file has the same shape, whether or not the
    // particular position has passed the hour mark.
    [[nodiscard]] std::wstring FormatPlaybackTime(double seconds, bool withHours);
}
