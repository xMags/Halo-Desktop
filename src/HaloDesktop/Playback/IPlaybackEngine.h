#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace HaloDesktop::Playback
{
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

        bool operator==(TrackInfo const&) const = default;
    };

    struct PlaybackState final
    {
        double PositionSeconds{};
        double DurationSeconds{};
        double Volume{ 68.0 };
        double Speed{ 1.0 };
        bool Paused{};
        bool Buffering{};
        std::uint64_t FileSerial{};
        std::uint64_t EndSerial{};
        PlaybackEndReason EndReason{ PlaybackEndReason::None };
        std::vector<TrackInfo> Tracks;
    };

    struct SubtitleStyle final{double Scale{1.0};std::wstring Font{L"Segoe UI"};double BorderSize{3.0};double ShadowOffset{2.0};};

    using PlaybackChangedToken = std::uint64_t;
    using PlaybackChangedHandler = std::function<void()>;

    class IPlaybackEngine
    {
    public:
        virtual ~IPlaybackEngine() = default;
        virtual void Start() = 0;
        virtual void Stop() noexcept = 0;
        virtual void Open(std::wstring const& source) = 0;
        virtual void AttachVideoWindow(std::uintptr_t windowHandle) = 0;
        virtual void DetachVideoWindow() noexcept = 0;
        virtual void SetPaused(bool paused) = 0;
        virtual void SeekAbsolute(double seconds) = 0;
        virtual void SeekRelative(double seconds) = 0;
        virtual void SetVolume(double volume) = 0;
        virtual void SetSpeed(double speed) = 0;
        virtual void SetAudioTrack(std::int64_t id) = 0;
        virtual void SetSubtitleTrack(std::optional<std::int64_t> id) = 0;
        virtual void SetSubtitleDelay(double seconds) = 0;
        virtual void SetAudioDelay(double seconds) = 0;
        virtual void AddExternalSubtitle(std::wstring const& path,std::wstring const& identityTitle) = 0;
        virtual void RemoveTrack(std::int64_t id) = 0;
        virtual void ApplySubtitleStyle(SubtitleStyle const& style) = 0;
        [[nodiscard]] virtual PlaybackState State() const = 0;
        [[nodiscard]] virtual double DurationNow() const noexcept = 0;
        virtual PlaybackChangedToken AddChangedHandler(PlaybackChangedHandler handler) = 0;
        virtual void RemoveChangedHandler(PlaybackChangedToken token) noexcept = 0;
    };
} // namespace HaloDesktop::Playback
