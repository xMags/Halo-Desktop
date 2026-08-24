#pragma once

#include "Api/Dto.h"
#include "Playback/IPlaybackEngine.h"

#include <functional>
#include <memory>
#include <optional>
#include <ppltasks.h>
#include <winrt/HaloDesktop.h>
#include <winrt/Microsoft.UI.Dispatching.h>

namespace HaloDesktop::Services
{
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
        PlaybackSessionController(std::shared_ptr<IPlaybackEngine> engine,std::shared_ptr<Services::WatchStateService> watchState);
        ~PlaybackSessionController();
        [[nodiscard]] concurrency::task<void> StartAsync(winrt::HaloDesktop::PlaybackRequest request);
        [[nodiscard]] concurrency::task<void> CloseAsync();
        void Stop()noexcept;
        void SetErrorHandler(std::function<void()> handler);
        void SetEndOfFileHandler(std::function<void()> handler);

    private:
        void OnEngineChanged();
        void ReportNow()noexcept;
        [[nodiscard]] concurrency::task<void> ReportWithTimeoutAsync();
        [[nodiscard]] bool IsPlaying(PlaybackState const& state)const noexcept;
        void ApplyResume();

        std::shared_ptr<IPlaybackEngine>m_engine;
        std::shared_ptr<Services::WatchStateService>m_watchState;
        std::shared_ptr<WatchReporter>m_reporter;
        winrt::HaloDesktop::PlaybackRequest m_request{nullptr};
        std::optional<Api::Dto::WatchEntry>m_prior;
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_reportTimer{nullptr};
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer::Tick_revoker m_reportTickRevoker{};
        PlaybackChangedToken m_engineToken{};
        PlaybackState m_lastState;
        std::function<void()>m_errorHandler;
        std::function<void()>m_endOfFileHandler;
        std::uint64_t m_seenFileSerial{},m_seenEndSerial{},m_startVersion{};
        bool m_started{},m_closing{};
    };
}
