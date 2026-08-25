#pragma once

#include "Playback/IPlaybackEngine.h"

#include <chrono>
#include <unordered_map>
#include <winrt/Microsoft.UI.Dispatching.h>

namespace HaloDesktop::Playback
{
    // UI-thread-only fallback engine. All state changes and callbacks run on the
    // DispatcherQueue that calls Start.
    class NullEngine final : public IPlaybackEngine
    {
    public:
        NullEngine();
        ~NullEngine() override;

        NullEngine(NullEngine const&) = delete;
        NullEngine& operator=(NullEngine const&) = delete;
        NullEngine(NullEngine&&) = delete;
        NullEngine& operator=(NullEngine&&) = delete;

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
        void Tick();
        void NotifyChanged();

        PlaybackState m_state;
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_timer{ nullptr };
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer::Tick_revoker m_tickRevoker{};
        std::unordered_map<PlaybackChangedToken, PlaybackChangedHandler> m_handlers;
        std::chrono::steady_clock::time_point m_lastTick{};
        PlaybackChangedToken m_nextToken{};
        PlaybackSource m_source;
        std::uintptr_t m_windowHandle{};
        std::int64_t m_audioTrack{ 1 };
        std::optional<std::int64_t> m_subtitleTrack{ 1 };
        double m_subtitleDelay{};
        double m_audioDelay{};
        bool m_running{};
        std::int64_t m_nextTrackId{100};
        SubtitleStyle m_subtitleStyle;
    };
} // namespace HaloDesktop::Playback
