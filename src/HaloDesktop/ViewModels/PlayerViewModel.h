#pragma once

#include "Playback/IPlaybackEngine.h"
#include "Playback/PlaybackPolicy.h"
#include "Playback/SubtitleController.h"
#include "PlaybackTrackViewModel.g.h"
#include "AddonSubtitleViewModel.g.h"
#include "SubtitleAppearanceViewModel.g.h"
#include "PlayerViewModel.g.h"
#include "Services/AppServices.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <vector>
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
    struct AddonSubtitleViewModel:AddonSubtitleViewModelT<AddonSubtitleViewModel>{AddonSubtitleViewModel(::HaloDesktop::Playback::AddonSubtitleDisplay value,bool selected);winrt::hstring Key()const;winrt::hstring Language()const;winrt::hstring Addon()const;winrt::hstring Variant()const;Microsoft::UI::Xaml::Visibility HashMatchVisibility()const noexcept;Microsoft::UI::Xaml::Visibility NameMatchVisibility()const noexcept;Microsoft::UI::Xaml::Visibility SelectedVisibility()const noexcept;private: ::HaloDesktop::Playback::AddonSubtitleDisplay m_value;bool m_selected{};};
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

    // Subtitle appearance for the playback panel. Owns no state of its own beyond the
    // cached copy it shows: every setter writes the account settings and then asks the
    // subtitle controller to restyle the running file.
    struct SubtitleAppearanceViewModel : SubtitleAppearanceViewModelT<SubtitleAppearanceViewModel>
    {
        SubtitleAppearanceViewModel(
            std::shared_ptr<::HaloDesktop::Services::SettingsSyncService> settings,
            std::shared_ptr<::HaloDesktop::Playback::SubtitleController> subtitles);
        [[nodiscard]] double Size() const noexcept;
        void Size(double value);
        [[nodiscard]] winrt::hstring SizeLabel() const;
        [[nodiscard]] bool IsDefaultFont() const noexcept;
        [[nodiscard]] bool IsSystemFont() const noexcept;
        [[nodiscard]] bool IsSerifFont() const noexcept;
        [[nodiscard]] bool IsMonoFont() const noexcept;
        [[nodiscard]] bool IsNoOutline() const noexcept;
        [[nodiscard]] bool IsThinOutline() const noexcept;
        [[nodiscard]] bool IsNormalOutline() const noexcept;
        [[nodiscard]] bool IsThickOutline() const noexcept;
        [[nodiscard]] bool Shadow() const noexcept;
        void Shadow(bool value);
        [[nodiscard]] bool TrackStyling() const noexcept;
        void TrackStyling(bool value);
        [[nodiscard]] winrt::hstring PreviewText() const;
        [[nodiscard]] double PreviewFontSize() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Media::FontFamily PreviewFontFamily() const;
        [[nodiscard]] double PreviewOutlineOffset() const noexcept;
        [[nodiscard]] double PreviewOutlineNegativeOffset() const noexcept;
        [[nodiscard]] double PreviewShadowOffset() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PreviewOutlineVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PreviewShadowVisibility() const noexcept;
        void SetFont(std::int32_t index);
        void SetOutline(std::int32_t index);
        void Refresh();
        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        void Raise(wchar_t const* propertyName);
        void RaisePreviewMetrics();
        void Apply();

        std::shared_ptr<::HaloDesktop::Services::SettingsSyncService> m_settings;
        std::shared_ptr<::HaloDesktop::Playback::SubtitleController> m_subtitles;
        double m_size{ 100.0 };
        std::int32_t m_fontIndex{ 1 };
        std::int32_t m_outlineIndex{ 2 };
        bool m_shadow{ true };
        bool m_trackStyling{ true };
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
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
        [[nodiscard]] winrt::hstring QualityTierText() const;
        [[nodiscard]] winrt::hstring QualityDetailText() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility QualityBadgeVisibility() const noexcept;
        [[nodiscard]] double QualityBadgeFontSize() const noexcept;
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
        [[nodiscard]] winrt::hstring UpNextArt() const;
        [[nodiscard]] winrt::hstring SubtitleDelayText() const;
        [[nodiscard]] winrt::hstring AudioDelayText() const;
        [[nodiscard]] bool IsPaused() const noexcept;
        [[nodiscard]] bool IsFullscreen() const noexcept;
        [[nodiscard]] bool UpNextOpen() const noexcept;
        [[nodiscard]] bool AudioTabSelected() const noexcept;
        [[nodiscard]] bool SubtitleTabSelected() const noexcept;
        [[nodiscard]] bool SubtitleTracksTabSelected() const noexcept;
        [[nodiscard]] bool SubtitleAppearanceTabSelected() const noexcept;
        [[nodiscard]] bool SpeedTabSelected() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PausedVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PlayingVisibility() const noexcept;
        [[nodiscard]] bool IsBuffering() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility BufferingVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility BufferingLabelVisibility() const noexcept;
        [[nodiscard]] double BufferingRingSize() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility EnterFullscreenIconVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ExitFullscreenIconVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PanelVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility AudioPanelVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SubtitlePanelVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SubtitleTracksVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SubtitleAppearanceVisibility() const noexcept;
        [[nodiscard]] winrt::HaloDesktop::SubtitleAppearanceViewModel SubtitleAppearance() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SpeedPanelVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility UpNextVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility UpNextAvailableVisibility() const noexcept;
        void SetPlayNextHandler(std::function<void()> handler);
        void SetCloseRequestedHandler(std::function<void()> handler);
        void SetSubtitleTrackHandler(std::function<void(std::int64_t)> handler);
        void SetSubtitlesOffHandler(std::function<void()> handler);
        void SetAddonSubtitleHandler(std::function<void(winrt::hstring)>handler);
        void SetAddonSubtitles(std::vector<::HaloDesktop::Playback::AddonSubtitleDisplay> values);
        // Answers which addon choice, if any, is the live subtitle. Owned by the
        // subtitle controller so identities never reach the view model.
        void SetAddonSelectionProvider(std::function<winrt::hstring()> provider);
        void SetUpNext(
            winrt::hstring const& title,
            winrt::hstring const& episodeLabel,
            winrt::hstring const& poster,
            winrt::hstring const& still);
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
        void SelectSubtitleTab(std::int32_t index);
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
        void RebuildAddonSubtitles();
        void Raise(wchar_t const* propertyName);
        void RestartHideTimer();
        void StopUpNextTimer() noexcept;
        void ApplyBufferingState();
        void SetBufferingVisible(bool visible);
        void StartBufferingTimer(std::chrono::milliseconds delay);
        void StopBufferingTimer() noexcept;
        [[nodiscard]] bool KeepsOsdVisible() const noexcept;
        [[nodiscard]] bool HasUpNext() const noexcept;
        [[nodiscard]] double ScrubTarget(double seconds) const noexcept;
        static winrt::hstring FormatTime(double seconds, bool withHours);

        std::shared_ptr<::HaloDesktop::Playback::IPlaybackEngine> m_engine;
        std::shared_ptr<::HaloDesktop::Shell::WindowPresentationService> m_windowPresentation;
        ::HaloDesktop::Playback::PlaybackChangedToken m_engineToken{};
        ::HaloDesktop::Playback::PlaybackState m_state;
        // Classified once per format change rather than per notification: the engine
        // reports several times a second and the format holds for a whole file.
        ::HaloDesktop::Playback::VideoQualityBadge m_qualityBadge;
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_hideTimer{ nullptr };
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_upNextTimer{ nullptr };
        // One-shot, reused for both edges of the buffering indicator: it delays the
        // show while a stall might still be instant, then holds a shown indicator.
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_bufferingTimer{ nullptr };
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer::Tick_revoker m_hideTickRevoker{};
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer::Tick_revoker m_upNextTickRevoker{};
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer::Tick_revoker m_bufferingTickRevoker{};
        std::chrono::steady_clock::time_point m_bufferingShownAt{};
        std::int32_t m_panelIndex{ -1 };
        std::int32_t m_subtitleTabIndex{ 0 };
        std::int32_t m_upNextRemaining{ 8 };
        std::int32_t m_subtitleDelayMs{};
        std::int32_t m_audioDelayMs{};
        winrt::hstring m_upNextTitle;
        winrt::hstring m_upNextEpisodeLabel;
        winrt::hstring m_upNextPoster;
        winrt::hstring m_upNextStill;
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
        std::vector<::HaloDesktop::Playback::AddonSubtitleDisplay> m_addonChoices;
        std::function<winrt::hstring()> m_addonSelectionProvider;
        double m_osdOpacity{ 1.0 };
        double m_scrubPosition{};
        std::chrono::steady_clock::time_point m_lastScrubSeek{};
        bool m_scrubbing{};
        bool m_stalled{};
        bool m_bufferingVisible{};
        bool m_upNextOpen{};
        bool m_upNextCountdown{};
        bool m_upNextClaimed{};
        bool m_active{};
        winrt::HaloDesktop::SubtitleAppearanceViewModel m_subtitleAppearance{ nullptr };
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
} // namespace winrt::HaloDesktop::implementation
