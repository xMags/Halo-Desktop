#include "pch.h"
#include "Controls/PlayerOsd.xaml.h"
#if __has_include("PlayerOsd.g.cpp")
#include "PlayerOsd.g.cpp"
#endif

#include "App.xaml.h"
#include "ViewModels/PlayerViewModel.h"

namespace winrt::HaloDesktop::implementation
{
    PlayerOsd::PlayerOsd() : m_viewModel(winrt::make<PlayerViewModel>(App::Services()))
    {
    }
    winrt::HaloDesktop::PlayerViewModel PlayerOsd::ViewModel() const
    {
        return m_viewModel;
    }
    void PlayerOsd::OnLoaded([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                             [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        auto const viewModel = winrt::get_self<PlayerViewModel>(m_viewModel);
        FindName(L"AudioTrackList")
            .as<Microsoft::UI::Xaml::Controls::ItemsControl>()
            .ItemsSource(viewModel->AudioTracksView());
        FindName(L"SubtitleTrackList")
            .as<Microsoft::UI::Xaml::Controls::ItemsControl>()
            .ItemsSource(viewModel->SubtitleTracksView());
    }
    void PlayerOsd::OnOsdPointerMoved([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                                      [[maybe_unused]] Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&)
    {
        m_viewModel.NotifyUserActivity();
    }
    void PlayerOsd::OnBackClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                                [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.ClosePlayer();
    }
    void PlayerOsd::OnTogglePauseClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                                       [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.TogglePause();
    }
    void PlayerOsd::OnReplayClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                                  [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.SeekRelative(-10.0);
    }
    void PlayerOsd::OnForwardClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                                   [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.SeekRelative(10.0);
    }
    void PlayerOsd::OnSeekPointerReleased(
        winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        auto const slider = sender.as<Microsoft::UI::Xaml::Controls::Slider>();
        m_viewModel.Position(slider.Value());
    }
    void PlayerOsd::OnFullscreenClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                                      [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.ToggleFullscreen();
    }
    void PlayerOsd::OnAudioPanelClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                                      [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.ShowPanel(0);
    }
    void PlayerOsd::OnSubtitlesPanelClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                                          [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.ShowPanel(1);
    }
    void PlayerOsd::OnSpeedPanelClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                                      [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.ShowPanel(2);
    }
    void PlayerOsd::OnUpNextClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                                  [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.ToggleUpNext();
    }
    void PlayerOsd::OnClosePanelClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                                      [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.ClosePanel();
    }
    void PlayerOsd::OnAudioTabClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                                    [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.SelectPanel(0);
    }
    void PlayerOsd::OnSubtitlesTabClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                                        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.SelectPanel(1);
    }
    void PlayerOsd::OnSpeedTabClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                                    [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.SelectPanel(2);
    }
    void PlayerOsd::OnAudioTrackClick(winrt::Windows::Foundation::IInspectable const& sender,
                                      [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.SelectAudio(sender.as<Microsoft::UI::Xaml::Controls::RadioButton>()
                                    .DataContext()
                                    .as<winrt::HaloDesktop::PlaybackTrackViewModel>()
                                    .Id());
    }
    void PlayerOsd::OnSubtitlesOffClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                                        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.DisableSubtitles();
    }
    void PlayerOsd::OnSubtitleTrackClick(winrt::Windows::Foundation::IInspectable const& sender,
                                         [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.SelectSubtitle(sender.as<Microsoft::UI::Xaml::Controls::RadioButton>()
                                       .DataContext()
                                       .as<winrt::HaloDesktop::PlaybackTrackViewModel>()
                                       .Id());
    }
    void PlayerOsd::OnSubtitleDelayDownClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                                             [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.AdjustSubtitleDelay(-100);
    }
    void PlayerOsd::OnSubtitleDelayUpClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                                           [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.AdjustSubtitleDelay(100);
    }
    void PlayerOsd::OnAudioDelayDownClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                                          [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.AdjustAudioDelay(-100);
    }
    void PlayerOsd::OnAudioDelayUpClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                                        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.AdjustAudioDelay(100);
    }
    void PlayerOsd::OnSpeedHalfClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                                     [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.SetSpeed(0.5);
    }
    void PlayerOsd::OnSpeedThreeQuarterClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                                             [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.SetSpeed(0.75);
    }
    void PlayerOsd::OnSpeedNormalClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                                       [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.SetSpeed(1.0);
    }
    void PlayerOsd::OnSpeedOneQuarterClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                                           [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.SetSpeed(1.25);
    }
    void PlayerOsd::OnSpeedOneHalfClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                                        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.SetSpeed(1.5);
    }
    void PlayerOsd::OnSpeedDoubleClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                                       [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.SetSpeed(2.0);
    }
    void PlayerOsd::OnCancelUpNextClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                                        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.CancelUpNext();
    }
    void PlayerOsd::OnPlayNextClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                                    [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.PlayNext();
    }
} // namespace winrt::HaloDesktop::implementation
