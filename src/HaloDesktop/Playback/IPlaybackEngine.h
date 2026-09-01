#pragma once

#include "Security/ProtectedHttpHeaders.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace HaloDesktop::Playback
{
    using PlaybackHeader = Security::ProtectedHttpHeader;

    struct PlaybackSource final
    {
        std::wstring Location;
        std::vector<PlaybackHeader> Headers;
    };

    enum class TrackType
    {
        Audio,
        Subtitle,
    };

    enum class PlaybackEndReason
    {
        None,
        Eof,
        Error,
        Stopped,
    };

    struct TrackInfo final
    {
        std::int64_t Id{};
        TrackType Type{ TrackType::Audio };
        std::wstring Title;
        std::wstring Note;
        std::wstring Codec;
        bool Selected{};
        bool External{};
        std::wstring Language;
        std::wstring Identity;

        bool operator==(TrackInfo const&) const = default;
    };

    enum class VideoDynamicRange
    {
        Standard,
        Hdr,
        Hlg,
        DolbyVision,
    };

    // Geometry and transfer characteristics of the running video. Width and height
    // are the display size after aspect correction, so anamorphic content is
    // described by what the viewer sees rather than by how it was stored.
    struct VideoFormat final
    {
        std::int32_t Width{};
        std::int32_t Height{};
        VideoDynamicRange DynamicRange{ VideoDynamicRange::Standard };

        bool operator==(VideoFormat const&) const = default;
    };

    // How the frame is placed in the window. Fit shows the whole frame and lets
    // bars appear wherever the video and window aspects disagree; Fill scales the
    // frame up until it covers the window and crops the overflow. Fill preserves
    // the aspect ratio, so it never stretches the picture to reach the edges.
    enum class VideoFitMode
    {
        Fit,
        Fill,
    };

    struct PlaybackState final
    {
        double PositionSeconds{};
        double DurationSeconds{};
        double Volume{ 68.0 };
        double Speed{ 1.0 };
        bool Paused{};
        bool Buffering{};
        // A seek whose new position is not live yet. Deliberately separate from
        // Buffering: the watch reporter reads Buffering as playback having stopped,
        // and a seek must not make it fire a progress report per keypress.
        bool SeekPending{};
        // Cleared by every Open and set once the engine reports the first frame of
        // that file. Tells a waiting caller whether the video surface is still black.
        bool FirstFrameReady{};
        bool TracksReady{};
        std::uint64_t FileSerial{};
        std::uint64_t EndSerial{};
        std::uint64_t SeekSerial{};
        std::uint64_t AudioSelectionSerial{};
        std::uint64_t SubtitleSelectionSerial{};
        PlaybackEndReason EndReason{ PlaybackEndReason::None };
        VideoFitMode VideoFit{ VideoFitMode::Fit };
        // Empty until the engine reports the first decoded video frame of the open
        // file, and for files that carry no video at all.
        std::optional<VideoFormat> Video;
        std::vector<TrackInfo> Tracks;
    };

    // An empty Font leaves the engine's own default face in place rather than naming
    // one. KeepTrackStyling asks the engine to respect a styled track's own
    // presentation; clearing it makes the fields above win over the track.
    struct SubtitleStyle final{double Scale{1.0};std::wstring Font{L"Segoe UI"};double BorderSize{3.0};double ShadowOffset{2.0};bool KeepTrackStyling{true};};

    struct VideoSurfaceSize final
    {
        std::uint32_t WidthPixels{ 1 };
        std::uint32_t HeightPixels{ 1 };

        bool operator==(VideoSurfaceSize const&) const = default;
    };

    // Receives the address of the IDXGISwapChain the engine presents into, or 0
    // when that swapchain is gone. Always invoked on the UI thread. The engine
    // owns the swapchain; the receiver must take its own COM reference if it
    // needs the object to outlive the call.
    using VideoSwapChainHandler = std::function<void(std::uintptr_t swapChainAddress)>;

    using PlaybackChangedToken = std::uint64_t;
    using PlaybackChangedHandler = std::function<void()>;

    class IPlaybackEngine
    {
    public:
        virtual ~IPlaybackEngine() = default;
        virtual void Start() = 0;
        virtual void Stop() noexcept = 0;
        virtual void Open(PlaybackSource source) = 0;
        virtual void Replay() = 0;
        // The surface must be attached before Start(). Attaching while running
        // restarts the engine on the new surface.
        virtual void AttachVideoSurface(VideoSurfaceSize size, VideoSwapChainHandler handler) = 0;
        virtual void SetVideoSurfaceSize(VideoSurfaceSize size) = 0;
        virtual void DetachVideoSurface() noexcept = 0;
        virtual void SetPaused(bool paused) = 0;
        virtual void SeekAbsolute(double seconds) = 0;
        virtual void SeekRelative(double seconds) = 0;
        virtual void SetVolume(double volume) = 0;
        virtual void SetSpeed(double speed) = 0;
        virtual void SetAudioTrack(std::int64_t id) = 0;
        virtual void SetSubtitleTrack(std::optional<std::int64_t> id) = 0;
        virtual void SetSubtitleDelay(double seconds) = 0;
        virtual void SetAudioDelay(double seconds) = 0;
        virtual void AddExternalSubtitle(
            std::wstring const& path,
            std::wstring const& identity,
            std::wstring const& displayTitle,
            std::wstring const& language) = 0;
        virtual void RemoveTrack(std::int64_t id) = 0;
        virtual void ApplySubtitleStyle(SubtitleStyle const& style) = 0;
        virtual void SetVideoFit(VideoFitMode mode) = 0;
        [[nodiscard]] virtual PlaybackState State() const = 0;
        [[nodiscard]] virtual double DurationNow() const noexcept = 0;
        virtual PlaybackChangedToken AddChangedHandler(PlaybackChangedHandler handler) = 0;
        virtual void RemoveChangedHandler(PlaybackChangedToken token) noexcept = 0;
    };
} // namespace HaloDesktop::Playback
