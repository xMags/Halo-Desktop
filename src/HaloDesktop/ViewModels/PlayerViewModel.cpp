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

#include "Shell/WindowPresentationService.h"
#include "Playback/PlaybackPolicy.h"
#include "ViewModels/ObservableHelper.h"

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

    // Preview seeks are frequent enough to feel continuous without flooding
    // libmpv with one command for every raw pointer update.
    constexpr std::chrono::milliseconds ScrubPreviewInterval{ 120 };
} // namespace

namespace winrt::HaloDesktop::implementation
{
    AddonSubtitleViewModel::AddonSubtitleViewModel(::HaloDesktop::Playback::AddonSubtitleDisplay value):m_value(std::move(value)){}winrt::hstring AddonSubtitleViewModel::Key()const{return m_value.Key;}winrt::hstring AddonSubtitleViewModel::Language()const{return m_value.Language;}winrt::hstring AddonSubtitleViewModel::Addon()const{return m_value.Addon;}winrt::hstring AddonSubtitleViewModel::Variant()const{return m_value.Variant;}
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

    PlayerViewModel::PlayerViewModel(::HaloDesktop::Services::AppServices const& services)
        : m_engine(services.Playback),
          m_windowPresentation(services.WindowPresentation), m_state(services.Playback->State()),
          m_audioTracks(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_subtitleTracks(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),m_addonSubtitles(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>())
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
        try
        {
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
        }
        catch (...)
        {
        }
        m_hideTickRevoker.revoke();
        m_upNextTickRevoker.revoke();
        m_hideTimer = nullptr;
        m_upNextTimer = nullptr;
        if (m_engineToken != 0)
        {
            m_engine->RemoveChangedHandler(m_engineToken);
        }
        m_engineToken = 0;
        m_engine->Stop();
        m_engine->DetachVideoWindow();
        try
        {
            if (m_windowPresentation->IsFullscreen())
            {
                m_windowPresentation->SetFullscreen(false);
            }
        }
        catch (...)
        {
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
    winrt::hstring PlayerViewModel::UpNextPoster() const{return m_upNextPoster;}
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
    Microsoft::UI::Xaml::Visibility PlayerViewModel::PausedVisibility() const noexcept
    {
        return m_state.Paused ? Visible : Collapsed;
    }
    Microsoft::UI::Xaml::Visibility PlayerViewModel::PlayingVisibility() const noexcept
    {
        return m_state.Paused ? Collapsed : Visible;
    }
    Microsoft::UI::Xaml::Visibility PlayerViewModel::EnterFullscreenIconVisibility() const noexcept
    {
        return m_windowPresentation->IsFullscreen() ? Collapsed : Visible;
    }
    Microsoft::UI::Xaml::Visibility PlayerViewModel::ExitFullscreenIconVisibility() const noexcept
    {
        return m_windowPresentation->IsFullscreen() ? Visible : Collapsed;
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
    void PlayerViewModel::SetAddonSubtitleHandler(std::function<void(winrt::hstring)>handler){m_addonSubtitleHandler=std::move(handler);}
    void PlayerViewModel::SetAddonSubtitles(std::vector<::HaloDesktop::Playback::AddonSubtitleDisplay>values){m_addonSubtitles.Clear();for(auto&value:values)m_addonSubtitles.Append(winrt::make<AddonSubtitleViewModel>(std::move(value)));Raise(L"AddonSubtitles");}
    void PlayerViewModel::SetUpNext(
        winrt::hstring const& title,
        winrt::hstring const& episodeLabel,
        winrt::hstring const& poster)
    {
        StopUpNextTimer();
        m_upNextTitle = title;
        m_upNextEpisodeLabel = episodeLabel;
        m_upNextPoster = poster;
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
        m_lastScrubSeek = std::chrono::steady_clock::now() - ScrubPreviewInterval;
        NotifyUserActivity();
    }
    void PlayerViewModel::ScrubTo(double seconds)
    {
        if (!m_scrubbing)
        {
            return;
        }

        m_scrubPosition = ScrubTarget(seconds);
        Raise(L"PositionText");
        auto const now = std::chrono::steady_clock::now();
        if (now - m_lastScrubSeek >= ScrubPreviewInterval)
        {
            m_lastScrubSeek = now;
            m_engine->SeekAbsolute(m_scrubPosition);
        }
        NotifyUserActivity();
    }
    void PlayerViewModel::EndScrub(double seconds)
    {
        if (!m_scrubbing)
        {
            return;
        }

        m_scrubPosition = ScrubTarget(seconds);
        m_scrubbing = false;
        m_engine->SeekAbsolute(m_scrubPosition);
        Raise(L"Position");
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
        m_engine->SetSubtitleTrack(id);
        NotifyUserActivity();
    }
    void PlayerViewModel::DisableSubtitles()
    {
        m_engine->SetSubtitleTrack(std::nullopt);
        NotifyUserActivity();
    }
    void PlayerViewModel::SelectAddonSubtitle(winrt::hstring const&key){if(m_addonSubtitleHandler)m_addonSubtitleHandler(key);NotifyUserActivity();}
    void PlayerViewModel::AdjustSubtitleDelay(std::int32_t milliseconds)
    {
        m_subtitleDelayMs = std::clamp(m_subtitleDelayMs + milliseconds, -5000, 5000);
        m_engine->SetSubtitleDelay(m_subtitleDelayMs / 1000.0);
        Raise(L"SubtitleDelayText");
        NotifyUserActivity();
    }
    void PlayerViewModel::AdjustAudioDelay(std::int32_t milliseconds)
    {
        m_audioDelayMs = std::clamp(m_audioDelayMs + milliseconds, -5000, 5000);
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
        m_windowPresentation->SetFullscreen(!m_windowPresentation->IsFullscreen());
        RaisePresentationMetrics();
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
            m_windowPresentation->SetFullscreen(false);
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
        m_state = std::move(nextState);
        if (tracksChanged)
        {
            RebuildTracks();
        }
        RaisePlaybackState();
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
                                     L"SpeedText", L"AudioSummary", L"SubtitleSummary", L"SubtitlesOffSelected",
                                     L"IsPaused", L"PausedVisibility", L"PlayingVisibility" })
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
               L"UpNextAvailableVisibility", L"UpNextProgress", L"UpNextKicker", L"UpNextTitle", L"UpNextEpisodeLabel", L"UpNextPoster" })
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
                                     L"HeaderPadding", L"TransportPadding", L"TitleFontSize" })
        {
            Raise(property);
        }
    }
    bool PlayerViewModel::KeepsOsdVisible() const noexcept
    {
        return m_state.Paused || m_panelIndex >= 0 || m_upNextOpen || m_scrubbing;
    }
    bool PlayerViewModel::HasUpNext() const noexcept
    {
        return !m_upNextClaimed && !m_upNextTitle.empty() && static_cast<bool>(m_playNextHandler);
    }
    double PlayerViewModel::ScrubTarget(double seconds) const noexcept
    {
        return std::clamp(seconds, 0.0, (std::max)(m_state.DurationSeconds, 0.0));
    }
    winrt::hstring PlayerViewModel::FormatTime(double seconds, bool withHours)
    {
        auto const totalSeconds = static_cast<std::int32_t>((std::max)(0.0, seconds));
        std::wostringstream value;
        if (withHours)
        {
            value << totalSeconds / 3600 << L":" << std::setw(2) << std::setfill(L'0')
                  << totalSeconds % 3600 / 60;
        }
        else
        {
            value << totalSeconds / 60;
        }
        value << L":" << std::setw(2) << std::setfill(L'0') << totalSeconds % 60;
        return winrt::hstring(value.str());
    }
    void PlayerViewModel::Raise(wchar_t const* propertyName)
    {
        ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, propertyName);
    }
} // namespace winrt::HaloDesktop::implementation
