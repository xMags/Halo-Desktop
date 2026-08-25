#pragma once

#include "Playback/IPlaybackEngine.h"
#include "Playback/SubtitleController.h"
#include "PlaybackTrackViewModel.g.h"
#include "AddonSubtitleViewModel.g.h"
#include "PlayerViewModel.g.h"
#include "Services/AppServices.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace HaloDesktop::Services
{
    class NavigationService;
}

namespace HaloDesktop::Shell
{
    class WindowPresentationService;
}

namespace winrt::HaloDesktop::implementation
{
    struct AddonSubtitleViewModel:AddonSubtitleViewModelT<AddonSubtitleViewModel>{explicit AddonSubtitleViewModel(::HaloDesktop::Playback::AddonSubtitleDisplay value);winrt::hstring Key()const;winrt::hstring Language()const;winrt::hstring Addon()const;winrt::hstring Variant()const;private: ::HaloDesktop::Playback::AddonSubtitleDisplay m_value;};
    struct PlaybackTrackViewModel : PlaybackTrackViewModelT<PlaybackTrackViewModel>
    {
        explicit PlaybackTrackViewModel(::HaloDesktop::Playback::TrackInfo track);
        [[nodiscard]] std::int64_t Id() const noexcept;
        [[nodiscard]] winrt::hstring Title() const;
        [[nodiscard]] winrt::hstring Note() const;
        [[nodiscard]] winrt::hstring Codec() const;
        [[nodiscard]] bool IsSelected() const noexcept;

    private:
        ::HaloDesktop::Playback::TrackInfo m_track;
    };

    struct PlayerViewModel : PlayerViewModelT<PlayerViewModel>
    {
        explicit PlayerViewModel(::HaloDesktop::Services::AppServices const& services);
        ~PlayerViewModel();
        void Activate();
        void Deactivate() noexcept;
        [[nodiscard]] double Position() const noexcept;
        [[nodiscard]] double Duration() const noexcept;
        [[nodiscard]] double Volume() const noexcept;
        void Volume(double value);
        [[nodiscard]] winrt::hstring VolumeText() const;
        [[nodiscard]] winrt::hstring PositionText() const;
        [[nodiscard]] winrt::hstring DurationText() const;
        [[nodiscard]] winrt::hstring SpeedText() const;
        [[nodiscard]] bool IsSpeedThreeQuarter() const noexcept;
        [[nodiscard]] bool IsSpeedNormal() const noexcept;
        [[nodiscard]] bool IsSpeedOneQuarter() const noexcept;
        [[nodiscard]] bool IsSpeedOneHalf() const noexcept;
        [[nodiscard]] bool IsSpeedOneThreeQuarter() const noexcept;
        [[nodiscard]] bool IsSpeedDouble() const noexcept;
        [[nodiscard]] winrt::hstring AudioSummary() const;
        [[nodiscard]] winrt::hstring SubtitleSummary() const;
        [[nodiscard]] double PlayButtonSize() const noexcept;
        [[nodiscard]] double TransportButtonSize() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Thickness HeaderPadding() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Thickness TransportPadding() const noexcept;
        [[nodiscard]] double TitleFontSize() const noexcept;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable AudioTracks() const;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable SubtitleTracks() const;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable AddonSubtitles()const;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<
            winrt::Windows::Foundation::IInspectable>
        AudioTracksView() const;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<
            winrt::Windows::Foundation::IInspectable>
        SubtitleTracksView() const;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> AddonSubtitlesView()const;
        [[nodiscard]] bool SubtitlesOffSelected() const noexcept;
        [[nodiscard]] double OsdOpacity() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility OsdWakeVisibility() const noexcept;
        [[nodiscard]] double UpNextProgress() const noexcept;
        [[nodiscard]] winrt::hstring UpNextKicker() const;
        [[nodiscard]] winrt::hstring UpNextTitle() const;
        [[nodiscard]] winrt::hstring UpNextEpisodeLabel() const;
        [[nodiscard]] winrt::hstring UpNextPoster() const;
        [[nodiscard]] winrt::hstring SubtitleDelayText() const;
        [[nodiscard]] winrt::hstring AudioDelayText() const;
        [[nodiscard]] bool IsPaused() const noexcept;
        [[nodiscard]] bool IsFullscreen() const noexcept;
        [[nodiscard]] bool UpNextOpen() const noexcept;
        [[nodiscard]] bool AudioTabSelected() const noexcept;
        [[nodiscard]] bool SubtitleTabSelected() const noexcept;
        [[nodiscard]] bool SpeedTabSelected() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PausedVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PlayingVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility EnterFullscreenIconVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ExitFullscreenIconVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PanelVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility AudioPanelVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SubtitlePanelVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SpeedPanelVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility UpNextVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility UpNextAvailableVisibility() const noexcept;
        void SetPlayNextHandler(std::function<void()> handler);
        void SetCloseRequestedHandler(std::function<void()> handler);
        void SetSubtitleTrackHandler(std::function<void(std::int64_t)> handler);
        void SetSubtitlesOffHandler(std::function<void()> handler);
        void SetAddonSubtitleHandler(std::function<void(winrt::hstring)>handler);
        void SetAddonSubtitles(std::vector<::HaloDesktop::Playback::AddonSubtitleDisplay> values);
        void SetUpNext(
            winrt::hstring const& title,
            winrt::hstring const& episodeLabel,
            winrt::hstring const& poster);
        void BeginUpNextCountdown();
        void TogglePause();
        void BeginScrub();
        void ScrubTo(double seconds);
        void EndScrub(double seconds);
        void SeekRelative(double seconds);
        void ChangeVolume(double delta);
        void SetSpeed(double speed);
        void ShowPanel(std::int32_t index);
        void SelectPanel(std::int32_t index);
        void ClosePanel();
        void SelectAudio(std::int64_t id);
        void SelectSubtitle(std::int64_t id);
        void DisableSubtitles();
        void SelectAddonSubtitle(winrt::hstring const&key);
        void AdjustSubtitleDelay(std::int32_t milliseconds);
        void AdjustAudioDelay(std::int32_t milliseconds);
        void ToggleUpNext();
        void CancelUpNext();
        void PlayNext();
        void ToggleFullscreen();
        void HandleEscape();
        void ClosePlayer();
        void NotifyUserActivity();
        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        void SynchronizeEngine();
        void RaisePlaybackState();
        void RaisePanelState();
        void RaisePresentationMetrics();
        void RebuildTracks();
        void Raise(wchar_t const* propertyName);
        void RestartHideTimer();
        void StopUpNextTimer() noexcept;
        [[nodiscard]] bool KeepsOsdVisible() const noexcept;
        [[nodiscard]] bool HasUpNext() const noexcept;
        [[nodiscard]] double ScrubTarget(double seconds) const noexcept;
        static winrt::hstring FormatTime(double seconds, bool withHours);

        std::shared_ptr<::HaloDesktop::Playback::IPlaybackEngine> m_engine;
        std::shared_ptr<::HaloDesktop::Shell::WindowPresentationService> m_windowPresentation;
        ::HaloDesktop::Playback::PlaybackChangedToken m_engineToken{};
        ::HaloDesktop::Playback::PlaybackState m_state;
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_hideTimer{ nullptr };
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_upNextTimer{ nullptr };
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer::Tick_revoker m_hideTickRevoker{};
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer::Tick_revoker m_upNextTickRevoker{};
        std::int32_t m_panelIndex{ -1 };
        std::int32_t m_upNextRemaining{ 8 };
        std::int32_t m_subtitleDelayMs{};
        std::int32_t m_audioDelayMs{};
        winrt::hstring m_upNextTitle;
        winrt::hstring m_upNextEpisodeLabel;
        winrt::hstring m_upNextPoster;
        std::function<void()> m_playNextHandler;
        std::function<void()> m_closeRequestedHandler;
        std::function<void(std::int64_t)> m_subtitleTrackHandler;
        std::function<void()> m_subtitlesOffHandler;
        std::function<void(winrt::hstring)>m_addonSubtitleHandler;
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable>
            m_audioTracks{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable>
            m_subtitleTracks{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable>m_addonSubtitles{nullptr};
        double m_osdOpacity{ 1.0 };
        double m_scrubPosition{};
        std::chrono::steady_clock::time_point m_lastScrubSeek{};
        bool m_scrubbing{};
        bool m_upNextOpen{};
        bool m_upNextCountdown{};
        bool m_upNextClaimed{};
        bool m_active{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
} // namespace winrt::HaloDesktop::implementation
