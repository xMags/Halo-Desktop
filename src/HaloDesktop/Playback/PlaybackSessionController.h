#pragma once

#include "Api/Dto.h"
#include "Playback/IPlaybackEngine.h"

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <ppltasks.h>
#include <winrt/HaloDesktop.h>
#include <winrt/Microsoft.UI.Dispatching.h>

namespace HaloDesktop::Services
{
    class SettingsSyncService;
    class WatchStateService;
}

namespace HaloDesktop::Playback
{
    class WatchReporter;

    // UI-thread-only playback lifecycle coordinator. It owns reporting,
    // resume, and the final-report-before-stop ordering, while the view model
    // remains responsible only for interactive controls.
    class PlaybackSessionController final : public std::enable_shared_from_this<PlaybackSessionController>
    {
    public:
        PlaybackSessionController(
            std::shared_ptr<IPlaybackEngine> engine,
            std::shared_ptr<Services::WatchStateService> watchState,
            std::shared_ptr<Services::SettingsSyncService> settings);
        ~PlaybackSessionController();
        [[nodiscard]] concurrency::task<void> StartAsync(winrt::HaloDesktop::PlaybackRequest request);
        [[nodiscard]] concurrency::task<void> CloseAsync();
        void Stop()noexcept;
        void SetErrorHandler(std::function<void()> handler);
        void SetEndOfFileHandler(std::function<void()> handler);
        void RefreshPreferences();

    private:
        void OnEngineChanged();
        void ReportNow()noexcept;
        [[nodiscard]] concurrency::task<void> ReportWithTimeoutAsync();
        [[nodiscard]] concurrency::task<void> LoadWatchStateAsync(std::uint64_t version);
        [[nodiscard]] bool IsPlaying(PlaybackState const& state)const noexcept;
        void ApplyResume();
        void ApplyAudioPreference();

        std::shared_ptr<IPlaybackEngine>m_engine;
        std::shared_ptr<Services::WatchStateService>m_watchState;
        std::shared_ptr<Services::SettingsSyncService>m_settings;
        std::shared_ptr<WatchReporter>m_reporter;
        winrt::HaloDesktop::PlaybackRequest m_request{nullptr};
        std::optional<Api::Dto::WatchEntry>m_prior;
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_reportTimer{nullptr};
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer::Tick_revoker m_reportTickRevoker{};
        PlaybackChangedToken m_engineToken{};
        PlaybackState m_lastState;
        std::function<void()>m_errorHandler;
        std::function<void()>m_endOfFileHandler;
        std::optional<winrt::hstring>m_preferredAudio;
        std::chrono::steady_clock::time_point m_resumeDeadline{};
        std::uint64_t m_seenFileSerial{},m_seenEndSerial{},m_startVersion{},m_initialSeekSerial{},m_initialAudioSelectionSerial{},m_autoAudioSelectionSerial{},m_audioPreferenceRevision{},m_appliedAudioPreferenceRevision{},m_appliedAudioFileSerial{};
        bool m_started{},m_closing{},m_fileReady{},m_watchLoadFinished{};
    };
}
