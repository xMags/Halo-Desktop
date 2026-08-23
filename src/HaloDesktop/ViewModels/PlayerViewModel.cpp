#include "pch.h"
#include "ViewModels/PlayerViewModel.h"
#if __has_include("PlaybackTrackViewModel.g.cpp")
#include "PlaybackTrackViewModel.g.cpp"
#endif
#if __has_include("PlayerViewModel.g.cpp")
#include "PlayerViewModel.g.cpp"
#endif

#include "Services/NavigationService.h"
#include "Shell/WindowPresentationService.h"
#include "ViewModels/ObservableHelper.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <winrt/Windows.UI.h>

namespace
{
    auto const Visible = winrt::Microsoft::UI::Xaml::Visibility::Visible;
    auto const Collapsed = winrt::Microsoft::UI::Xaml::Visibility::Collapsed;
} // namespace

namespace winrt::HaloDesktop::implementation
{
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
        : m_engine(services.Playback), m_navigation(services.Navigation),
          m_windowPresentation(services.WindowPresentation), m_state(services.Playback->State()),
          m_audioTracks(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_subtitleTracks(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>())
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
                        self->CancelUpNext();
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
        return m_state.PositionSeconds;
    }
    void PlayerViewModel::Position(double value)
    {
        m_engine->SeekAbsolute(value);
        NotifyUserActivity();
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
    winrt::hstring PlayerViewModel::TimeText() const
    {
        return winrt::hstring(std::wstring(FormatTime(m_state.PositionSeconds)) + L" / " +
                              std::wstring(FormatTime(m_state.DurationSeconds)));
    }
    winrt::hstring PlayerViewModel::SpeedText() const
    {
        std::wostringstream value;
        value << std::setprecision(3) << m_state.Speed << L"×";
        return winrt::hstring(value.str());
    }
    winrt::hstring PlayerViewModel::PlayPauseGlyph() const
    {
        return m_state.Paused ? L"\uE768" : L"\uE769";
    }
    winrt::hstring PlayerViewModel::FullscreenGlyph() const
    {
        return m_windowPresentation->IsFullscreen() ? L"\uE73F" : L"\uE740";
    }
    double PlayerViewModel::PlayButtonSize() const noexcept
    {
        return m_windowPresentation->IsFullscreen() ? 54.0 : 44.0;
    }
    Microsoft::UI::Xaml::Media::Brush PlayerViewModel::AudioChipBackground() const
    {
        return ChipBackground(m_panelIndex == 0);
    }
    Microsoft::UI::Xaml::Media::Brush PlayerViewModel::SubtitleChipBackground() const
    {
        return ChipBackground(m_panelIndex == 1);
    }
    Microsoft::UI::Xaml::Media::Brush PlayerViewModel::SpeedChipBackground() const
    {
        return ChipBackground(m_panelIndex == 2);
    }
    Microsoft::UI::Xaml::Media::Brush PlayerViewModel::UpNextChipBackground() const
    {
        return ChipBackground(m_upNextOpen);
    }
    winrt::Windows::Foundation::IInspectable PlayerViewModel::AudioTracks() const
    {
        return m_audioTracks;
    }
    winrt::Windows::Foundation::IInspectable PlayerViewModel::SubtitleTracks() const
    {
        return m_subtitleTracks;
    }
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
    double PlayerViewModel::BufferedPosition() const noexcept
    {
        return (std::min)(m_state.DurationSeconds, m_state.PositionSeconds + m_state.DurationSeconds * 0.09);
    }
    double PlayerViewModel::OsdOpacity() const noexcept
    {
        return m_osdOpacity;
    }
    double PlayerViewModel::UpNextProgress() const noexcept
    {
        return static_cast<double>(m_upNextRemaining) / 8.0 * 100.0;
    }
    winrt::hstring PlayerViewModel::UpNextKicker() const
    {
        return winrt::hstring(L"UP NEXT IN " + std::to_wstring(m_upNextRemaining) + L" S");
    }
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
        return m_upNextOpen ? Visible : Collapsed;
    }
    void PlayerViewModel::TogglePause()
    {
        m_engine->SetPaused(!m_state.Paused);
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
        if (m_upNextOpen)
        {
            CancelUpNext();
            return;
        }
        m_panelIndex = -1;
        m_upNextOpen = true;
        m_upNextRemaining = 8;
        m_upNextTimer.Start();
        RaisePanelState();
        NotifyUserActivity();
    }
    void PlayerViewModel::CancelUpNext()
    {
        if (!m_upNextOpen)
        {
            return;
        }
        StopUpNextTimer();
        m_upNextOpen = false;
        m_upNextRemaining = 8;
        RaisePanelState();
        RestartHideTimer();
    }
    void PlayerViewModel::PlayNext()
    {
        CancelUpNext();
    }
    void PlayerViewModel::ToggleFullscreen()
    {
        m_windowPresentation->SetFullscreen(!m_windowPresentation->IsFullscreen());
        Raise(L"IsFullscreen");
        Raise(L"FullscreenGlyph");
        Raise(L"PlayButtonSize");
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
        m_navigation->CloseOverlay();
    }
    void PlayerViewModel::NotifyUserActivity()
    {
        if (m_osdOpacity != 1.0)
        {
            m_osdOpacity = 1.0;
            Raise(L"OsdOpacity");
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
        for (auto const property : { L"Position", L"Duration", L"Volume", L"VolumeText", L"TimeText", L"SpeedText",
                                     L"PlayPauseGlyph", L"BufferedPosition", L"IsPaused", L"PausedVisibility" })
        {
            Raise(property);
        }
    }
    void PlayerViewModel::RaisePanelState()
    {
        for (auto const property :
             { L"PanelVisibility", L"AudioPanelVisibility", L"SubtitlePanelVisibility", L"SpeedPanelVisibility",
               L"AudioTabSelected", L"SubtitleTabSelected", L"SpeedTabSelected", L"AudioChipBackground",
               L"SubtitleChipBackground", L"SpeedChipBackground", L"UpNextChipBackground", L"UpNextVisibility",
               L"UpNextProgress", L"UpNextKicker" })
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
    bool PlayerViewModel::KeepsOsdVisible() const noexcept
    {
        return m_state.Paused || m_panelIndex >= 0 || m_upNextOpen;
    }
    winrt::hstring PlayerViewModel::FormatTime(double seconds)
    {
        auto const totalSeconds = static_cast<std::int32_t>((std::max)(0.0, seconds));
        std::wostringstream value;
        value << totalSeconds / 60 << L":" << std::setw(2) << std::setfill(L'0') << totalSeconds % 60;
        return winrt::hstring(value.str());
    }
    Microsoft::UI::Xaml::Media::Brush PlayerViewModel::ChipBackground(bool active) const
    {
        auto const color = active ? winrt::Windows::UI::Color{ 0x66, 0x60, 0xCD, 0xFF }
                                  : winrt::Windows::UI::Color{ 0x99, 0x18, 0x18, 0x1A };
        return Microsoft::UI::Xaml::Media::SolidColorBrush{ color };
    }
    void PlayerViewModel::Raise(wchar_t const* propertyName)
    {
        ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, propertyName);
    }
} // namespace winrt::HaloDesktop::implementation
