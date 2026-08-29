#include "pch.h"
#include "ViewModels/PlayerViewModel.h"
#if __has_include("PlaybackTrackViewModel.g.cpp")
#include "PlaybackTrackViewModel.g.cpp"
#endif
#if __has_include("PlayerViewModel.g.cpp")
#include "PlayerViewModel.g.cpp"
#endif
#if __has_include("AddonSubtitleViewModel.g.cpp")
#include "AddonSubtitleViewModel.g.cpp"
#endif
#if __has_include("SubtitleAppearanceViewModel.g.cpp")
#include "SubtitleAppearanceViewModel.g.cpp"
#endif

#include "Shell/WindowPresentationService.h"
#include "Playback/PlaybackPolicy.h"
#include "Services/SettingsSyncService.h"
#include "ViewModels/ObservableHelper.h"
#include "ViewModels/ScrubPreviewViewModel.h"
#include "ViewModels/SubtitlePreviewMetrics.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace
{
    auto const Visible = winrt::Microsoft::UI::Xaml::Visibility::Visible;
    auto const Collapsed = winrt::Microsoft::UI::Xaml::Visibility::Collapsed;

} // namespace

namespace winrt::HaloDesktop::implementation
{
    AddonSubtitleViewModel::AddonSubtitleViewModel(::HaloDesktop::Playback::AddonSubtitleDisplay value,bool selected):m_value(std::move(value)),m_selected(selected){}winrt::hstring AddonSubtitleViewModel::Key()const{return m_value.Key;}winrt::hstring AddonSubtitleViewModel::Language()const{return m_value.Language;}winrt::hstring AddonSubtitleViewModel::Addon()const{return m_value.Addon;}winrt::hstring AddonSubtitleViewModel::Variant()const{return m_value.Variant;}
    Microsoft::UI::Xaml::Visibility AddonSubtitleViewModel::HashMatchVisibility()const noexcept{return m_value.HashMatched?Visible:Collapsed;}
    Microsoft::UI::Xaml::Visibility AddonSubtitleViewModel::NameMatchVisibility()const noexcept{return m_value.HashMatched?Collapsed:Visible;}
    Microsoft::UI::Xaml::Visibility AddonSubtitleViewModel::SelectedVisibility()const noexcept{return m_selected?Visible:Collapsed;}
    PlaybackTrackViewModel::PlaybackTrackViewModel(::HaloDesktop::Playback::TrackInfo track) : m_track(std::move(track))
    {
    }
    std::int64_t PlaybackTrackViewModel::Id() const noexcept
    {
        return m_track.Id;
    }
    winrt::hstring PlaybackTrackViewModel::Title() const
    {
        return winrt::hstring(m_track.Title);
    }
    winrt::hstring PlaybackTrackViewModel::Note() const
    {
        return winrt::hstring(m_track.Note);
    }
    winrt::hstring PlaybackTrackViewModel::Codec() const
    {
        return winrt::hstring(m_track.Codec);
    }
    bool PlaybackTrackViewModel::IsSelected() const noexcept
    {
        return m_track.Selected;
    }


    SubtitleAppearanceViewModel::SubtitleAppearanceViewModel(
        std::shared_ptr<::HaloDesktop::Services::SettingsSyncService> settings,
        std::shared_ptr<::HaloDesktop::Playback::SubtitleController> subtitles)
        : m_settings(std::move(settings)), m_subtitles(std::move(subtitles))
    {
        Refresh();
    }
    void SubtitleAppearanceViewModel::Refresh()
    {
        if (!m_settings) return;
        m_size = m_settings->SubtitleScalePercent();
        m_fontIndex = static_cast<std::int32_t>(
            ::HaloDesktop::ViewModels::SubtitleFontIndex(m_settings->SubtitleFontFamily()));
        m_outlineIndex = static_cast<std::int32_t>(
            ::HaloDesktop::ViewModels::SubtitleOutlineIndex(m_settings->SubtitleOutline()));
        m_shadow = m_settings->SubtitleShadow();
        m_trackStyling = m_settings->SubtitleTrackStyling();
        for (auto const property : { L"Size", L"SizeLabel", L"IsDefaultFont", L"IsSystemFont", L"IsSerifFont",
                                     L"IsMonoFont", L"IsNoOutline", L"IsThinOutline", L"IsNormalOutline",
                                     L"IsThickOutline", L"Shadow", L"TrackStyling", L"PreviewFontFamily",
                                     L"PreviewOutlineVisibility", L"PreviewShadowVisibility" })
        {
            Raise(property);
        }
        RaisePreviewMetrics();
    }
    // Style only. RefreshPreferences would also re-run track selection, which would drop
    // and reload the subtitle mid-drag every time the size slider ticks.
    void SubtitleAppearanceViewModel::Apply() { if (m_subtitles) m_subtitles->RefreshStyle(); }
    double SubtitleAppearanceViewModel::Size() const noexcept { return m_size; }
    void SubtitleAppearanceViewModel::Size(double value)
    {
        auto const next = std::clamp(value, 50.0, 200.0);
        if (std::abs(next - m_size) < 0.01) return;
        m_size = next;
        m_settings->SubtitleScalePercent(static_cast<std::int32_t>(std::lround(next)));
        Raise(L"Size");
        Raise(L"SizeLabel");
        RaisePreviewMetrics();
        Apply();
    }
    winrt::hstring SubtitleAppearanceViewModel::SizeLabel() const
    {
        std::wostringstream label;
        label << static_cast<std::int32_t>(std::lround(m_size)) << L"%";
        return winrt::hstring(label.str());
    }
    bool SubtitleAppearanceViewModel::IsDefaultFont() const noexcept { return m_fontIndex == 0; }
    bool SubtitleAppearanceViewModel::IsSystemFont() const noexcept { return m_fontIndex == 1; }
    bool SubtitleAppearanceViewModel::IsSerifFont() const noexcept { return m_fontIndex == 2; }
    bool SubtitleAppearanceViewModel::IsMonoFont() const noexcept { return m_fontIndex == 3; }
    bool SubtitleAppearanceViewModel::IsNoOutline() const noexcept { return m_outlineIndex == 0; }
    bool SubtitleAppearanceViewModel::IsThinOutline() const noexcept { return m_outlineIndex == 1; }
    bool SubtitleAppearanceViewModel::IsNormalOutline() const noexcept { return m_outlineIndex == 2; }
    bool SubtitleAppearanceViewModel::IsThickOutline() const noexcept { return m_outlineIndex == 3; }
    void SubtitleAppearanceViewModel::SetFont(std::int32_t index)
    {
        if (index < 0 || index > 3 || index == m_fontIndex) return;
        m_fontIndex = index;
        m_settings->SubtitleFontFamily(winrt::hstring{
            ::HaloDesktop::ViewModels::kSubtitleFontFamilies[static_cast<std::size_t>(index)] });
        for (auto const property : { L"IsDefaultFont", L"IsSystemFont", L"IsSerifFont", L"IsMonoFont",
                                     L"PreviewFontFamily" })
        {
            Raise(property);
        }
        Apply();
    }
    void SubtitleAppearanceViewModel::SetOutline(std::int32_t index)
    {
        if (index < 0 || index > 3 || index == m_outlineIndex) return;
        m_outlineIndex = index;
        m_settings->SubtitleOutline(winrt::hstring{
            ::HaloDesktop::ViewModels::kSubtitleOutlines[static_cast<std::size_t>(index)] });
        for (auto const property : { L"IsNoOutline", L"IsThinOutline", L"IsNormalOutline", L"IsThickOutline",
                                     L"PreviewOutlineVisibility" })
        {
            Raise(property);
        }
        RaisePreviewMetrics();
        Apply();
    }
    bool SubtitleAppearanceViewModel::Shadow() const noexcept { return m_shadow; }
    void SubtitleAppearanceViewModel::Shadow(bool value)
    {
        if (m_shadow == value) return;
        m_shadow = value;
        m_settings->SubtitleShadow(value);
        Raise(L"Shadow");
        Raise(L"PreviewShadowVisibility");
        Apply();
    }
    bool SubtitleAppearanceViewModel::TrackStyling() const noexcept { return m_trackStyling; }
    void SubtitleAppearanceViewModel::TrackStyling(bool value)
    {
        if (m_trackStyling == value) return;
        m_trackStyling = value;
        m_settings->SubtitleTrackStyling(value);
        Raise(L"TrackStyling");
        Apply();
    }
    // Long enough to wrap in the panel, so the preview shows what a real two-line caption
    // does to the picture rather than flattering the settings with a short string.
    winrt::hstring SubtitleAppearanceViewModel::PreviewText() const { return L"They kept the antenna pointed at nothing for eleven years."; }
    double SubtitleAppearanceViewModel::PreviewFontSize() const noexcept { return ::HaloDesktop::ViewModels::SubtitlePreviewFontSize(m_size); }
    Microsoft::UI::Xaml::Media::FontFamily SubtitleAppearanceViewModel::PreviewFontFamily() const
    {
        if (m_fontIndex == 2) return Microsoft::UI::Xaml::Media::FontFamily{ L"Georgia" };
        if (m_fontIndex == 3) return Microsoft::UI::Xaml::Media::FontFamily{ L"ms-appx:///Assets/Fonts/JetBrainsMono-Regular.ttf#JetBrains Mono" };
        // Default hands the face to mpv, which lands on a system sans, so the preview
        // shows that rather than pretending to know what a styled track will pick.
        return Microsoft::UI::Xaml::Media::FontFamily{ L"Segoe UI Variable Text" };
    }
    double SubtitleAppearanceViewModel::PreviewOutlineOffset() const noexcept
    {
        return ::HaloDesktop::ViewModels::SubtitlePreviewOutlineOffset(
            static_cast<std::size_t>(std::clamp(m_outlineIndex, 0, 3)), m_size);
    }
    double SubtitleAppearanceViewModel::PreviewOutlineNegativeOffset() const noexcept { return -PreviewOutlineOffset(); }
    double SubtitleAppearanceViewModel::PreviewShadowOffset() const noexcept { return ::HaloDesktop::ViewModels::SubtitlePreviewShadowOffset(m_size); }
    Microsoft::UI::Xaml::Visibility SubtitleAppearanceViewModel::PreviewOutlineVisibility() const noexcept { return m_outlineIndex > 0 ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility SubtitleAppearanceViewModel::PreviewShadowVisibility() const noexcept { return m_shadow ? Visible : Collapsed; }
    void SubtitleAppearanceViewModel::RaisePreviewMetrics()
    {
        for (auto const property : { L"PreviewFontSize", L"PreviewOutlineOffset",
                                     L"PreviewOutlineNegativeOffset", L"PreviewShadowOffset" })
        {
            Raise(property);
        }
    }
    winrt::event_token SubtitleAppearanceViewModel::PropertyChanged(
        Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
    {
        return m_propertyChanged.add(handler);
    }
    void SubtitleAppearanceViewModel::PropertyChanged(winrt::event_token const& token) noexcept
    {
        m_propertyChanged.remove(token);
    }
    void SubtitleAppearanceViewModel::Raise(wchar_t const* propertyName)
    {
        ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, propertyName);
    }

    PlayerViewModel::PlayerViewModel(::HaloDesktop::Services::AppServices const& services)
        : m_engine(services.Playback),
          m_windowPresentation(services.WindowPresentation), m_state(services.Playback->State()),
          m_audioTracks(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_subtitleTracks(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),m_addonSubtitles(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_subtitleAppearance(winrt::make<SubtitleAppearanceViewModel>(services.SettingsSync, services.Subtitles)),
          m_scrubPreview(winrt::make<ScrubPreviewViewModel>(services.ScrubPreview))
    {
        RebuildTracks();
    }
    PlayerViewModel::~PlayerViewModel()
    {
        Deactivate();
    }
    void PlayerViewModel::Activate()
    {
        if (m_active)
        {
            return;
        }

        auto const dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        if (!dispatcher)
        {
            throw winrt::hresult_wrong_thread();
        }

        m_hideTimer = dispatcher.CreateTimer();
        m_hideTimer.Interval(std::chrono::seconds(3));
        m_hideTimer.IsRepeating(false);
        m_hideTickRevoker = m_hideTimer.Tick(
            winrt::auto_revoke,
            [weak = get_weak()]([[maybe_unused]] winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer const& timer,
                                [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& args) {
                if (auto const self = weak.get())
                {
                    if (!self->KeepsOsdVisible())
                    {
                        self->m_osdOpacity = 0.0;
                        ::HaloDesktop::detail::RaisePropertyChanged(self->m_propertyChanged, *self, L"OsdOpacity");
                        ::HaloDesktop::detail::RaisePropertyChanged(self->m_propertyChanged, *self,
                                                                    L"OsdWakeVisibility");
                    }
                }
            });
        m_bufferingTimer = dispatcher.CreateTimer();
        m_bufferingTimer.IsRepeating(false);
        m_bufferingTickRevoker = m_bufferingTimer.Tick(
            winrt::auto_revoke,
            [weak = get_weak()]([[maybe_unused]] winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer const& timer,
                                [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& args) {
                // The timer serves whichever edge was outstanding, so the stall state
                // it finds is the answer for both.
                if (auto const self = weak.get())
                {
                    self->SetBufferingVisible(self->m_stalled);
                }
            });
        m_upNextTimer = dispatcher.CreateTimer();
        m_upNextTimer.Interval(std::chrono::seconds(1));
        m_upNextTimer.IsRepeating(true);
        m_upNextTickRevoker = m_upNextTimer.Tick(
            winrt::auto_revoke,
            [weak = get_weak()]([[maybe_unused]] winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer const& timer,
                                [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& args) {
                if (auto const self = weak.get())
                {
                    --self->m_upNextRemaining;
                    if (self->m_upNextRemaining <= 0)
                    {
                        self->PlayNext();
                        return;
                    }
                    ::HaloDesktop::detail::RaisePropertyChanged(self->m_propertyChanged, *self, L"UpNextProgress");
                    ::HaloDesktop::detail::RaisePropertyChanged(self->m_propertyChanged, *self, L"UpNextKicker");
                }
            });
        m_engineToken = m_engine->AddChangedHandler([weak = get_weak()]() {
            if (auto const self = weak.get())
            {
                self->SynchronizeEngine();
            }
        });
        m_active = true;
        if (m_scrubPreview)
        {
            winrt::get_self<ScrubPreviewViewModel>(m_scrubPreview)->Activate();
        }
        try
        {
            m_engine->SetSpeed(1.0);
            m_engine->Start();
            SynchronizeEngine();
            RestartHideTimer();
        }
        catch (...)
        {
            Deactivate();
            throw;
        }
    }
    void PlayerViewModel::Deactivate() noexcept
    {
        if (!m_active)
        {
            return;
        }

        m_active = false;
        try
        {
            if (m_hideTimer)
            {
                m_hideTimer.Stop();
            }
            if (m_upNextTimer)
            {
                m_upNextTimer.Stop();
            }
            if (m_bufferingTimer)
            {
                m_bufferingTimer.Stop();
            }
        }
        catch (...)
        {
        }
        if (m_scrubPreview)
        {
            winrt::get_self<ScrubPreviewViewModel>(m_scrubPreview)->Deactivate();
        }
        m_hideTickRevoker.revoke();
        m_upNextTickRevoker.revoke();
        m_bufferingTickRevoker.revoke();
        m_hideTimer = nullptr;
        m_upNextTimer = nullptr;
        m_bufferingTimer = nullptr;
        m_stalled = false;
        // Best-effort: Deactivate is noexcept, but a handler may still be attached and
        // a re-activated view model must not inherit a stale visible indicator.
        try
        {
            SetBufferingVisible(false);
        }
        catch (...)
        {
        }
        if (m_engineToken != 0)
        {
            m_engine->RemoveChangedHandler(m_engineToken);
        }
        m_engineToken = 0;
        m_engine->Stop();
        m_engine->DetachVideoWindow();
        if (m_windowPresentation->IsFullscreen())
        {
            static_cast<void>(m_windowPresentation->TrySetFullscreen(false));
        }
    }
    double PlayerViewModel::Position() const noexcept
    {
        return m_scrubbing ? m_scrubPosition : m_state.PositionSeconds;
    }
    double PlayerViewModel::Duration() const noexcept
    {
        return m_state.DurationSeconds;
    }
    double PlayerViewModel::Volume() const noexcept
    {
        return m_state.Volume;
    }
    void PlayerViewModel::Volume(double value)
    {
        m_engine->SetVolume(value);
        NotifyUserActivity();
    }
    winrt::hstring PlayerViewModel::VolumeText() const
    {
        return winrt::to_hstring(static_cast<std::int32_t>(std::lround(m_state.Volume)));
    }
    winrt::hstring PlayerViewModel::PositionText() const
    {
        return FormatTime(Position(), m_state.DurationSeconds >= 3600.0);
    }
    winrt::hstring PlayerViewModel::DurationText() const
    {
        return FormatTime(m_state.DurationSeconds, m_state.DurationSeconds >= 3600.0);
    }
    winrt::hstring PlayerViewModel::SpeedText() const
    {
        std::wostringstream value;
        value << std::setprecision(3) << m_state.Speed << L"×";
        return winrt::hstring(value.str());
    }
    bool PlayerViewModel::IsSpeedThreeQuarter()const noexcept{return ::HaloDesktop::Playback::IsPlaybackSpeedSelected(m_state.Speed,0.75);}
    bool PlayerViewModel::IsSpeedNormal()const noexcept{return ::HaloDesktop::Playback::IsPlaybackSpeedSelected(m_state.Speed,1.0);}
    bool PlayerViewModel::IsSpeedOneQuarter()const noexcept{return ::HaloDesktop::Playback::IsPlaybackSpeedSelected(m_state.Speed,1.25);}
    bool PlayerViewModel::IsSpeedOneHalf()const noexcept{return ::HaloDesktop::Playback::IsPlaybackSpeedSelected(m_state.Speed,1.5);}
    bool PlayerViewModel::IsSpeedOneThreeQuarter()const noexcept{return ::HaloDesktop::Playback::IsPlaybackSpeedSelected(m_state.Speed,1.75);}
    bool PlayerViewModel::IsSpeedDouble()const noexcept{return ::HaloDesktop::Playback::IsPlaybackSpeedSelected(m_state.Speed,2.0);}
    winrt::hstring PlayerViewModel::AudioSummary()const{return winrt::hstring(::HaloDesktop::Playback::TrackSummary(m_state.Tracks,::HaloDesktop::Playback::TrackType::Audio));}
    winrt::hstring PlayerViewModel::SubtitleSummary()const{return winrt::hstring(::HaloDesktop::Playback::TrackSummary(m_state.Tracks,::HaloDesktop::Playback::TrackType::Subtitle));}
    double PlayerViewModel::PlayButtonSize() const noexcept
    {
        return m_windowPresentation->IsFullscreen() ? 54.0 : 44.0;
    }
    double PlayerViewModel::TransportButtonSize() const noexcept
    {
        return m_windowPresentation->IsFullscreen() ? 40.0 : 34.0;
    }
    Microsoft::UI::Xaml::Thickness PlayerViewModel::HeaderPadding() const noexcept
    {
        return m_windowPresentation->IsFullscreen() ? Microsoft::UI::Xaml::Thickness{ 40.0, 30.0, 40.0, 30.0 }
                                                    : Microsoft::UI::Xaml::Thickness{ 22.0, 16.0, 22.0, 16.0 };
    }
    Microsoft::UI::Xaml::Thickness PlayerViewModel::TransportPadding() const noexcept
    {
        return m_windowPresentation->IsFullscreen() ? Microsoft::UI::Xaml::Thickness{ 44.0, 0.0, 44.0, 30.0 }
                                                    : Microsoft::UI::Xaml::Thickness{ 24.0, 0.0, 24.0, 18.0 };
    }
    double PlayerViewModel::TitleFontSize() const noexcept
    {
        return m_windowPresentation->IsFullscreen() ? 19.0 : 15.0;
    }
    winrt::hstring PlayerViewModel::QualityTierText() const
    {
        return winrt::hstring(m_qualityBadge.Tier);
    }
    winrt::hstring PlayerViewModel::QualityDetailText() const
    {
        return winrt::hstring(m_qualityBadge.Detail);
    }
    Microsoft::UI::Xaml::Visibility PlayerViewModel::QualityBadgeVisibility() const noexcept
    {
        // No tier means no video worth describing: an audio-only file, or a file
        // whose first frame has not been decoded yet.
        return m_qualityBadge.Tier.empty() ? Collapsed : Visible;
    }
    double PlayerViewModel::QualityBadgeFontSize() const noexcept
    {
        return m_windowPresentation->IsFullscreen() ? 12.5 : 10.5;
    }
    winrt::Windows::Foundation::IInspectable PlayerViewModel::AudioTracks() const
    {
        return m_audioTracks;
    }
    winrt::Windows::Foundation::IInspectable PlayerViewModel::SubtitleTracks() const
    {
        return m_subtitleTracks;
    }
    winrt::Windows::Foundation::IInspectable PlayerViewModel::AddonSubtitles()const{return m_addonSubtitles;}
    winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable>
    PlayerViewModel::AudioTracksView() const
    {
        return m_audioTracks;
    }
    winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable>
    PlayerViewModel::SubtitleTracksView() const
    {
        return m_subtitleTracks;
    }
    auto PlayerViewModel::AddonSubtitlesView()const->winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable>{return m_addonSubtitles;}
    bool PlayerViewModel::SubtitlesOffSelected()const noexcept{return std::none_of(m_state.Tracks.begin(),m_state.Tracks.end(),[](auto const&track){return track.Type==::HaloDesktop::Playback::TrackType::Subtitle&&track.Selected;});}
    double PlayerViewModel::OsdOpacity() const noexcept
    {
        return m_osdOpacity;
    }
    Microsoft::UI::Xaml::Visibility PlayerViewModel::OsdWakeVisibility() const noexcept
    {
        return m_osdOpacity < 1.0 ? Visible : Collapsed;
    }
    double PlayerViewModel::UpNextProgress() const noexcept
    {
        return static_cast<double>(m_upNextRemaining) / 8.0 * 100.0;
    }
    winrt::hstring PlayerViewModel::UpNextKicker() const
    {
        if (!m_upNextCountdown) return L"UP NEXT";
        return winrt::hstring(L"UP NEXT IN " + std::to_wstring(m_upNextRemaining) + L" S");
    }
    winrt::hstring PlayerViewModel::UpNextTitle() const
    {
        return m_upNextTitle;
    }
    winrt::hstring PlayerViewModel::UpNextEpisodeLabel() const{return m_upNextEpisodeLabel;}
    // The card is a 16:9 frame, so the episode's own still belongs in it. The
    // poster only stands in when the addon supplied no still, where it is centre
    // cropped rather than left blank.
    winrt::hstring PlayerViewModel::UpNextArt() const{return m_upNextStill.empty()?m_upNextPoster:m_upNextStill;}
    winrt::hstring PlayerViewModel::SubtitleDelayText() const
    {
        return winrt::hstring(std::to_wstring(m_subtitleDelayMs) + L" MS");
    }
    winrt::hstring PlayerViewModel::AudioDelayText() const
    {
        return winrt::hstring(std::to_wstring(m_audioDelayMs) + L" MS");
    }
    bool PlayerViewModel::IsPaused() const noexcept
    {
        return m_state.Paused;
    }
    bool PlayerViewModel::IsFullscreen() const noexcept
    {
        return m_windowPresentation->IsFullscreen();
    }
    bool PlayerViewModel::UpNextOpen() const noexcept
    {
        return m_upNextOpen;
    }
    bool PlayerViewModel::AudioTabSelected() const noexcept
    {
        return m_panelIndex == 0;
    }
    bool PlayerViewModel::SubtitleTabSelected() const noexcept
    {
        return m_panelIndex == 1;
    }
    bool PlayerViewModel::SpeedTabSelected() const noexcept
    {
        return m_panelIndex == 2;
    }
    bool PlayerViewModel::SubtitleTracksTabSelected() const noexcept
    {
        return m_subtitleTabIndex == 0;
    }
    bool PlayerViewModel::SubtitleAppearanceTabSelected() const noexcept
    {
        return m_subtitleTabIndex == 1;
    }
    Microsoft::UI::Xaml::Visibility PlayerViewModel::PausedVisibility() const noexcept
    {
        return m_state.Paused ? Visible : Collapsed;
    }
    Microsoft::UI::Xaml::Visibility PlayerViewModel::PlayingVisibility() const noexcept
    {
        return m_state.Paused ? Collapsed : Visible;
    }
    bool PlayerViewModel::IsBuffering() const noexcept
    {
        return m_bufferingVisible;
    }
    Microsoft::UI::Xaml::Visibility PlayerViewModel::BufferingVisibility() const noexcept
    {
        return m_bufferingVisible ? Visible : Collapsed;
    }
    Microsoft::UI::Xaml::Visibility PlayerViewModel::BufferingLabelVisibility() const noexcept
    {
        // Only worth a caption before the first frame, when the surface is black and
        // the ring is the only thing on screen.
        return m_state.FirstFrameReady ? Collapsed : Visible;
    }
    double PlayerViewModel::BufferingRingSize() const noexcept
    {
        auto const fullscreen = m_windowPresentation->IsFullscreen();
        if (!m_state.FirstFrameReady)
        {
            return fullscreen ? 72.0 : 56.0;
        }
        return fullscreen ? 44.0 : 36.0;
    }
    Microsoft::UI::Xaml::Visibility PlayerViewModel::EnterFullscreenIconVisibility() const noexcept
    {
        return m_windowPresentation->IsFullscreen() ? Collapsed : Visible;
    }
    Microsoft::UI::Xaml::Visibility PlayerViewModel::ExitFullscreenIconVisibility() const noexcept
    {
        return m_windowPresentation->IsFullscreen() ? Visible : Collapsed;
    }
    bool PlayerViewModel::IsVideoFilled() const noexcept
    {
        return m_state.VideoFit == ::HaloDesktop::Playback::VideoFitMode::Fill;
    }
    // The button shows the mode that is live, not the one a press would select, so
    // it reads the same way as the fullscreen button beside it.
    Microsoft::UI::Xaml::Visibility PlayerViewModel::FitIconVisibility() const noexcept
    {
        return IsVideoFilled() ? Collapsed : Visible;
    }
    Microsoft::UI::Xaml::Visibility PlayerViewModel::FillIconVisibility() const noexcept
    {
        return IsVideoFilled() ? Visible : Collapsed;
    }
    Microsoft::UI::Xaml::Visibility PlayerViewModel::PanelVisibility() const noexcept
    {
        return m_panelIndex >= 0 ? Visible : Collapsed;
    }
    Microsoft::UI::Xaml::Visibility PlayerViewModel::AudioPanelVisibility() const noexcept
    {
        return m_panelIndex == 0 ? Visible : Collapsed;
    }
    Microsoft::UI::Xaml::Visibility PlayerViewModel::SubtitlePanelVisibility() const noexcept
    {
        return m_panelIndex == 1 ? Visible : Collapsed;
    }
    Microsoft::UI::Xaml::Visibility PlayerViewModel::SpeedPanelVisibility() const noexcept
    {
        return m_panelIndex == 2 ? Visible : Collapsed;
    }
    Microsoft::UI::Xaml::Visibility PlayerViewModel::SubtitleTracksVisibility() const noexcept
    {
        return m_subtitleTabIndex == 0 ? Visible : Collapsed;
    }
    Microsoft::UI::Xaml::Visibility PlayerViewModel::SubtitleAppearanceVisibility() const noexcept
    {
        return m_subtitleTabIndex == 1 ? Visible : Collapsed;
    }
    winrt::HaloDesktop::SubtitleAppearanceViewModel PlayerViewModel::SubtitleAppearance() const
    {
        return m_subtitleAppearance;
    }
    winrt::HaloDesktop::ScrubPreviewViewModel PlayerViewModel::ScrubPreview() const
    {
        return m_scrubPreview;
    }
    Microsoft::UI::Xaml::Visibility PlayerViewModel::UpNextVisibility() const noexcept
    {
        return m_upNextOpen && HasUpNext() ? Visible : Collapsed;
    }
    Microsoft::UI::Xaml::Visibility PlayerViewModel::UpNextAvailableVisibility() const noexcept
    {
        return HasUpNext() ? Visible : Collapsed;
    }
    void PlayerViewModel::SetPlayNextHandler(std::function<void()> handler)
    {
        m_playNextHandler = std::move(handler);
        if (!HasUpNext() && m_upNextOpen)
        {
            CancelUpNext();
        }
        Raise(L"UpNextAvailableVisibility");
    }
    void PlayerViewModel::SetCloseRequestedHandler(std::function<void()> handler){m_closeRequestedHandler=std::move(handler);}
    void PlayerViewModel::SetSubtitleTrackHandler(std::function<void(std::int64_t)>handler){m_subtitleTrackHandler=std::move(handler);}
    void PlayerViewModel::SetSubtitlesOffHandler(std::function<void()>handler){m_subtitlesOffHandler=std::move(handler);}
    void PlayerViewModel::SetAddonSubtitleHandler(std::function<void(winrt::hstring)>handler){m_addonSubtitleHandler=std::move(handler);}
    void PlayerViewModel::SetAddonSubtitles(std::vector<::HaloDesktop::Playback::AddonSubtitleDisplay>values){m_addonChoices=std::move(values);RebuildAddonSubtitles();}
    void PlayerViewModel::SetAddonSelectionProvider(std::function<winrt::hstring()>provider){m_addonSelectionProvider=std::move(provider);RebuildAddonSubtitles();}
    void PlayerViewModel::SetUpNext(
        winrt::hstring const& title,
        winrt::hstring const& episodeLabel,
        winrt::hstring const& poster,
        winrt::hstring const& still)
    {
        StopUpNextTimer();
        m_upNextTitle = title;
        m_upNextEpisodeLabel = episodeLabel;
        m_upNextPoster = poster;
        m_upNextStill = still;
        m_upNextRemaining = 8;
        m_upNextOpen = false;
        m_upNextCountdown = false;
        m_upNextClaimed = false;
        RaisePanelState();
    }
    void PlayerViewModel::BeginUpNextCountdown()
    {
        if (!HasUpNext() || !m_upNextTimer) return;
        m_panelIndex = -1;
        m_upNextOpen = true;
        m_upNextCountdown = true;
        m_upNextRemaining = 8;
        m_upNextTimer.Start();
        RaisePanelState();
        NotifyUserActivity();
    }
    void PlayerViewModel::TogglePause()
    {
        if(m_state.EndReason==::HaloDesktop::Playback::PlaybackEndReason::Eof)
        {
            StopUpNextTimer();m_upNextOpen=false;m_upNextCountdown=false;m_upNextRemaining=8;m_engine->Replay();m_engine->SetPaused(false);RaisePanelState();NotifyUserActivity();return;
        }
        m_engine->SetPaused(!m_state.Paused);
        NotifyUserActivity();
    }
    void PlayerViewModel::BeginScrub()
    {
        if (m_scrubbing)
        {
            return;
        }

        m_scrubbing = true;
        m_scrubPosition = m_state.PositionSeconds;
        ApplyBufferingState();
        NotifyUserActivity();
    }
    void PlayerViewModel::ScrubTo(double seconds)
    {
        if (!m_scrubbing)
        {
            return;
        }

        // Deliberately does not move the engine. The scrub preview shows where the
        // drag is going, and the file is seeked once, on release: seeking the live
        // player per pointer update costs a range request and a cache flush each time
        // on a remote source.
        m_scrubPosition = ScrubTarget(seconds);
        Raise(L"PositionText");
        NotifyUserActivity();
    }
    void PlayerViewModel::EndScrub(double seconds)
    {
        if (!m_scrubbing)
        {
            return;
        }

        m_scrubPosition = ScrubTarget(seconds);
        // Keep engine notifications from changing the Slider while its pointer
        // release delegate is still completing the final seek.
        m_engine->SeekAbsolute(m_scrubPosition);
        m_scrubbing = false;
        ApplyBufferingState();
        // The Slider already owns this value. Its next engine notification can
        // refresh the binding after WinUI finishes pointer capture cleanup.
        Raise(L"PositionText");
        NotifyUserActivity();
    }
    void PlayerViewModel::SeekRelative(double seconds)
    {
        m_engine->SeekRelative(seconds);
        NotifyUserActivity();
    }
    void PlayerViewModel::ChangeVolume(double delta)
    {
        m_engine->SetVolume(m_state.Volume + delta);
        NotifyUserActivity();
    }
    void PlayerViewModel::SetSpeed(double speed)
    {
        m_engine->SetSpeed(speed);
        NotifyUserActivity();
    }
    void PlayerViewModel::ShowPanel(std::int32_t index)
    {
        if (index < 0 || index > 2)
        {
            return;
        }
        m_panelIndex = m_panelIndex == index ? -1 : index;
        if (m_panelIndex >= 0)
        {
            CancelUpNext();
        }
        RaisePanelState();
        NotifyUserActivity();
    }
    void PlayerViewModel::SelectPanel(std::int32_t index)
    {
        if (index < 0 || index > 2 || index == m_panelIndex)
        {
            return;
        }
        m_panelIndex = index;
        RaisePanelState();
        NotifyUserActivity();
    }
    void PlayerViewModel::SelectSubtitleTab(std::int32_t index)
    {
        if (index < 0 || index > 1 || index == m_subtitleTabIndex)
        {
            return;
        }
        m_subtitleTabIndex = index;
        // The stored appearance can have moved since the panel was last opened, from the
        // settings page or from another device's sync, so the tab reads it back rather
        // than trusting the copy it cached when the player started.
        if (index == 1 && m_subtitleAppearance)
        {
            m_subtitleAppearance.Refresh();
        }
        for (auto const property : { L"SubtitleTracksTabSelected", L"SubtitleAppearanceTabSelected",
                                     L"SubtitleTracksVisibility", L"SubtitleAppearanceVisibility" })
        {
            Raise(property);
        }
        NotifyUserActivity();
    }
    void PlayerViewModel::ClosePanel()
    {
        if (m_panelIndex >= 0)
        {
            m_panelIndex = -1;
            RaisePanelState();
            RestartHideTimer();
        }
    }
    void PlayerViewModel::SelectAudio(std::int64_t id)
    {
        m_engine->SetAudioTrack(id);
        NotifyUserActivity();
    }
    void PlayerViewModel::SelectSubtitle(std::int64_t id)
    {
        if(m_subtitleTrackHandler)m_subtitleTrackHandler(id);
        NotifyUserActivity();
    }
    void PlayerViewModel::DisableSubtitles()
    {
        if(m_subtitlesOffHandler)m_subtitlesOffHandler();
        NotifyUserActivity();
    }
    void PlayerViewModel::SelectAddonSubtitle(winrt::hstring const&key){if(m_addonSubtitleHandler)m_addonSubtitleHandler(key);NotifyUserActivity();}
    void PlayerViewModel::AdjustSubtitleDelay(std::int32_t milliseconds)
    {
        m_subtitleDelayMs = ::HaloDesktop::Playback::AdjustPlaybackDelayMilliseconds(m_subtitleDelayMs,milliseconds);
        m_engine->SetSubtitleDelay(m_subtitleDelayMs / 1000.0);
        Raise(L"SubtitleDelayText");
        NotifyUserActivity();
    }
    void PlayerViewModel::AdjustAudioDelay(std::int32_t milliseconds)
    {
        m_audioDelayMs = ::HaloDesktop::Playback::AdjustPlaybackDelayMilliseconds(m_audioDelayMs,milliseconds);
        m_engine->SetAudioDelay(m_audioDelayMs / 1000.0);
        Raise(L"AudioDelayText");
        NotifyUserActivity();
    }
    void PlayerViewModel::ToggleUpNext()
    {
        if (!HasUpNext())
        {
            return;
        }
        if (m_upNextOpen)
        {
            CancelUpNext();
            return;
        }
        m_panelIndex = -1;
        m_upNextOpen = true;
        m_upNextRemaining = 8;
        m_upNextCountdown = false;
        RaisePanelState();
        NotifyUserActivity();
    }
    void PlayerViewModel::CancelUpNext()
    {
        if (!m_upNextOpen || m_upNextClaimed)
        {
            return;
        }
        m_upNextClaimed = true;
        StopUpNextTimer();
        m_upNextOpen = false;
        m_upNextCountdown = false;
        m_upNextRemaining = 8;
        RaisePanelState();
        RestartHideTimer();
    }
    void PlayerViewModel::PlayNext()
    {
        if (!HasUpNext() || m_upNextClaimed)
        {
            return;
        }

        m_upNextClaimed = true;
        StopUpNextTimer();
        m_upNextOpen = false;
        m_upNextCountdown = false;
        m_upNextRemaining = 8;
        auto const playNext = m_playNextHandler;
        playNext();
        RaisePanelState();
        NotifyUserActivity();
    }
    void PlayerViewModel::ToggleFullscreen()
    {
        auto const fullscreen = !m_windowPresentation->IsFullscreen();
        if (!m_windowPresentation->TrySetFullscreen(fullscreen))
        {
            return;
        }
        RaisePresentationMetrics();
        NotifyUserActivity();
    }
    void PlayerViewModel::ToggleVideoFit()
    {
        using ::HaloDesktop::Playback::VideoFitMode;
        m_engine->SetVideoFit(IsVideoFilled() ? VideoFitMode::Fit : VideoFitMode::Fill);
        // The engine notification is what refreshes the icon; this only keeps the
        // OSD from fading out from under the press that caused it.
        NotifyUserActivity();
    }
    void PlayerViewModel::HandleEscape()
    {
        NotifyUserActivity();
        if (m_windowPresentation->IsFullscreen())
        {
            ToggleFullscreen();
            return;
        }
        if (m_panelIndex >= 0)
        {
            ClosePanel();
            return;
        }
        ClosePlayer();
    }
    void PlayerViewModel::ClosePlayer()
    {
        if (m_windowPresentation->IsFullscreen())
        {
            static_cast<void>(m_windowPresentation->TrySetFullscreen(false));
        }
        if(m_closeRequestedHandler)m_closeRequestedHandler();
    }
    void PlayerViewModel::NotifyUserActivity()
    {
        if (m_osdOpacity != 1.0)
        {
            m_osdOpacity = 1.0;
            Raise(L"OsdOpacity");
            Raise(L"OsdWakeVisibility");
        }
        RestartHideTimer();
    }
    winrt::event_token PlayerViewModel::PropertyChanged(
        Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
    {
        return m_propertyChanged.add(handler);
    }
    void PlayerViewModel::PropertyChanged(winrt::event_token const& token) noexcept
    {
        m_propertyChanged.remove(token);
    }
    void PlayerViewModel::SynchronizeEngine()
    {
        auto const wasPaused = m_state.Paused;
        auto nextState = m_engine->State();
        auto const tracksChanged = nextState.Tracks != m_state.Tracks;
        auto const videoChanged = nextState.Video != m_state.Video;
        auto const fileChanged = nextState.FileSerial != m_state.FileSerial;
        m_state = std::move(nextState);
        if (fileChanged && m_scrubPreview)
        {
            m_scrubPreview.Reset();
        }
        if (tracksChanged)
        {
            RebuildTracks();
        }
        if (videoChanged)
        {
            m_qualityBadge = m_state.Video ? ::HaloDesktop::Playback::ClassifyVideoQuality(*m_state.Video)
                                           : ::HaloDesktop::Playback::VideoQualityBadge{};
        }
        RaisePlaybackState();
        ApplyBufferingState();
        if (m_state.Paused != wasPaused)
        {
            if (m_state.Paused)
            {
                NotifyUserActivity();
            }
            else
            {
                RestartHideTimer();
            }
        }
    }
    void PlayerViewModel::RaisePlaybackState()
    {
        for (auto const property : { L"Duration", L"Volume", L"VolumeText", L"PositionText", L"DurationText",
                                     L"SpeedText", L"IsSpeedThreeQuarter", L"IsSpeedNormal", L"IsSpeedOneQuarter",
                                     L"IsSpeedOneHalf", L"IsSpeedOneThreeQuarter", L"IsSpeedDouble",
                                     L"AudioSummary", L"SubtitleSummary", L"SubtitlesOffSelected",
                                     L"IsPaused", L"PausedVisibility", L"PlayingVisibility",
                                     L"BufferingLabelVisibility", L"BufferingRingSize",
                                     L"QualityTierText", L"QualityDetailText", L"QualityBadgeVisibility",
                                     L"IsVideoFilled", L"FitIconVisibility", L"FillIconVisibility" })
        {
            Raise(property);
        }
        // The pointer owns the thumb during a scrub. Publishing engine position
        // notifications here would fight the gesture and snap it backwards.
        if (!m_scrubbing)
        {
            Raise(L"Position");
        }
    }
    void PlayerViewModel::RaisePanelState()
    {
        for (auto const property :
             { L"PanelVisibility", L"AudioPanelVisibility", L"SubtitlePanelVisibility", L"SpeedPanelVisibility",
               L"AudioTabSelected", L"SubtitleTabSelected", L"SpeedTabSelected", L"UpNextOpen", L"UpNextVisibility",
               L"UpNextAvailableVisibility", L"UpNextProgress", L"UpNextKicker", L"UpNextTitle", L"UpNextEpisodeLabel", L"UpNextArt" })
        {
            Raise(property);
        }
    }
    void PlayerViewModel::RebuildTracks()
    {
        m_audioTracks.Clear();
        m_subtitleTracks.Clear();
        for (auto const& track : m_state.Tracks)
        {
            auto const row = winrt::make<PlaybackTrackViewModel>(track);
            if (track.Type == ::HaloDesktop::Playback::TrackType::Audio)
            {
                m_audioTracks.Append(row);
            }
            else
            {
                m_subtitleTracks.Append(row);
            }
        }
        RebuildAddonSubtitles();
    }
    // Rebuilt rather than notified per item: the list is short, and the addon
    // rows have to re-read the live selection whenever the engine's tracks move.
    void PlayerViewModel::RebuildAddonSubtitles()
    {
        auto const selected = m_addonSelectionProvider ? m_addonSelectionProvider() : winrt::hstring{};
        m_addonSubtitles.Clear();
        for (auto const& value : m_addonChoices)
        {
            m_addonSubtitles.Append(winrt::make<AddonSubtitleViewModel>(value, !selected.empty() && value.Key == selected));
        }
        Raise(L"AddonSubtitles");
    }
    void PlayerViewModel::RestartHideTimer()
    {
        if (!m_active || !m_hideTimer)
        {
            return;
        }
        m_hideTimer.Stop();
        if (!KeepsOsdVisible())
        {
            m_hideTimer.Start();
        }
    }
    void PlayerViewModel::StopUpNextTimer() noexcept
    {
        try
        {
            if (m_upNextTimer)
            {
                m_upNextTimer.Stop();
            }
        }
        catch (...)
        {
        }
    }
    void PlayerViewModel::RaisePresentationMetrics()
    {
        for (auto const property : { L"IsFullscreen", L"EnterFullscreenIconVisibility",
                                     L"ExitFullscreenIconVisibility", L"PlayButtonSize", L"TransportButtonSize",
                                     L"HeaderPadding", L"TransportPadding", L"TitleFontSize",
                                     L"BufferingRingSize", L"QualityBadgeFontSize" })
        {
            Raise(property);
        }
    }
    // Debounced on both edges. A cached seek resolves in tens of milliseconds and a
    // marginal connection makes the cache flag flap, so an undelayed indicator would
    // flash for a frame or strobe. Opening a file skips the delay because the surface
    // is still black and there is nothing to flicker against.
    void PlayerViewModel::ApplyBufferingState()
    {
        // A drag leaves the engine where it was, so any stall reported during one
        // belongs to the position the viewer has already left. The indicator waits for
        // the release and the single seek that follows it.
        auto const stalled = !m_scrubbing
            && ::HaloDesktop::Playback::IsPlaybackStalled(
                m_state.Buffering,
                m_state.SeekPending,
                m_state.Paused);
        if (stalled == m_stalled)
        {
            return;
        }
        m_stalled = stalled;
        StopBufferingTimer();
        if (stalled)
        {
            if (m_bufferingVisible)
            {
                return;
            }
            auto const delay = ::HaloDesktop::Playback::BufferingIndicatorDelay(m_state.FirstFrameReady);
            if (delay <= std::chrono::milliseconds::zero())
            {
                SetBufferingVisible(true);
                return;
            }
            StartBufferingTimer(delay);
            return;
        }
        if (!m_bufferingVisible)
        {
            return;
        }
        auto const shownFor = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - m_bufferingShownAt);
        auto const remaining = ::HaloDesktop::Playback::BufferingIndicatorHoldRemaining(shownFor);
        if (remaining <= std::chrono::milliseconds::zero())
        {
            SetBufferingVisible(false);
            return;
        }
        StartBufferingTimer(remaining);
    }
    void PlayerViewModel::SetBufferingVisible(bool visible)
    {
        if (visible == m_bufferingVisible)
        {
            return;
        }
        m_bufferingVisible = visible;
        if (visible)
        {
            m_bufferingShownAt = std::chrono::steady_clock::now();
        }
        Raise(L"IsBuffering");
        Raise(L"BufferingVisibility");
    }
    void PlayerViewModel::StartBufferingTimer(std::chrono::milliseconds delay)
    {
        if (!m_bufferingTimer)
        {
            return;
        }
        m_bufferingTimer.Interval(delay);
        m_bufferingTimer.Start();
    }
    void PlayerViewModel::StopBufferingTimer() noexcept
    {
        try
        {
            if (m_bufferingTimer)
            {
                m_bufferingTimer.Stop();
            }
        }
        catch (...)
        {
        }
    }
    bool PlayerViewModel::KeepsOsdVisible() const noexcept
    {
        // Reached through the implementation rather than the projection: this function
        // is noexcept, and a projected call is a boundary that is allowed to throw.
        return m_state.Paused || m_panelIndex >= 0 || m_upNextOpen || m_scrubbing
            || (m_scrubPreview && winrt::get_self<ScrubPreviewViewModel>(m_scrubPreview)->IsOpen());
    }
    bool PlayerViewModel::HasUpNext() const noexcept
    {
        return !m_upNextClaimed && !m_upNextTitle.empty() && static_cast<bool>(m_playNextHandler);
    }
    double PlayerViewModel::ScrubTarget(double seconds) const noexcept
    {
        return ::HaloDesktop::Playback::NormalizePlaybackTimeline(
            seconds,
            m_state.DurationSeconds).PositionSeconds;
    }
    winrt::hstring PlayerViewModel::FormatTime(double seconds, bool withHours)
    {
        return winrt::hstring(::HaloDesktop::Playback::FormatPlaybackTime(seconds, withHours));
    }
    void PlayerViewModel::Raise(wchar_t const* propertyName)
    {
        ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, propertyName);
    }
} // namespace winrt::HaloDesktop::implementation
