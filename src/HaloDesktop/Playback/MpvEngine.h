#pragma once

#include "Playback/IPlaybackEngine.h"

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
        void Open(std::wstring const& source) override;
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
        [[nodiscard]] PlaybackState State() const override;
        PlaybackChangedToken AddChangedHandler(PlaybackChangedHandler handler) override;
        void RemoveChangedHandler(PlaybackChangedToken token) noexcept override;

    private:
        void ApplyUpdate(PlaybackUpdate update);
        void NotifyChanged();

        std::unique_ptr<MpvClient> m_client;
        PlaybackState m_state;
        std::unordered_map<PlaybackChangedToken, PlaybackChangedHandler> m_handlers;
        PlaybackChangedToken m_nextToken{};
        std::wstring m_source;
        std::uintptr_t m_windowHandle{};
        bool m_running{};
    };
} // namespace HaloDesktop::Playback
