#pragma once

#include "Playback/IPlaybackEngine.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace HaloDesktop::Playback
{
    class MpvClient;
    struct PlaybackUpdate;

    // UI-thread-only adapter that keeps libmpv behind the player engine contract.
    class MpvEngine final : public IPlaybackEngine, public std::enable_shared_from_this<MpvEngine>
    {
    public:
        MpvEngine();
        ~MpvEngine() override;

        MpvEngine(MpvEngine const&) = delete;
        MpvEngine& operator=(MpvEngine const&) = delete;
        MpvEngine(MpvEngine&&) = delete;
        MpvEngine& operator=(MpvEngine&&) = delete;

        void Start() override;
        void Stop() noexcept override;
        void Open(PlaybackSource source) override;
        void Replay() override;
        void AttachVideoWindow(std::uintptr_t windowHandle) override;
        void DetachVideoWindow() noexcept override;
        void SetPaused(bool paused) override;
        void SeekAbsolute(double seconds) override;
        void SeekRelative(double seconds) override;
        void SetVolume(double volume) override;
        void SetSpeed(double speed) override;
        void SetAudioTrack(std::int64_t id) override;
        void SetSubtitleTrack(std::optional<std::int64_t> id) override;
        void SetSubtitleDelay(double seconds) override;
        void SetAudioDelay(double seconds) override;
        void AddExternalSubtitle(
            std::wstring const& path,
            std::wstring const& identity,
            std::wstring const& displayTitle,
            std::wstring const& language) override;
        void RemoveTrack(std::int64_t id) override;
        void ApplySubtitleStyle(SubtitleStyle const& style) override;
        [[nodiscard]] PlaybackState State() const override;
        [[nodiscard]] double DurationNow() const noexcept override;
        PlaybackChangedToken AddChangedHandler(PlaybackChangedHandler handler) override;
        void RemoveChangedHandler(PlaybackChangedToken token) noexcept override;

    private:
        void ApplyUpdate(PlaybackUpdate update);
        void SynchronizeAudioSession();
        void NotifyChanged();
        // Keep the newest commanded position on screen until libmpv restarts at that
        // target, so an older time-pos or restart cannot rewind a rapid scrub.
        void BeginSeek(double targetSeconds) noexcept;
        [[nodiscard]] bool AcceptPosition(double positionSeconds) noexcept;

        std::unique_ptr<MpvClient> m_client;
        PlaybackState m_state;
        std::unordered_map<PlaybackChangedToken, PlaybackChangedHandler> m_handlers;
        PlaybackChangedToken m_nextToken{};
        PlaybackSource m_source;
        std::uintptr_t m_windowHandle{};
        std::chrono::steady_clock::time_point m_seekIssuedAt{};
        std::optional<double> m_seekTarget;
        bool m_seekRestarted{};
        bool m_running{};
        bool m_pausedForCache{};
        std::uint64_t m_audioSessionSerial{};
        SubtitleStyle m_subtitleStyle;
    };
} // namespace HaloDesktop::Playback
