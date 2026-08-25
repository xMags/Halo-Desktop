#include "pch.h"
#include "Controls/PlayerOsd.xaml.h"
#if __has_include("PlayerOsd.g.cpp")
#include "PlayerOsd.g.cpp"
#endif

#include "App.xaml.h"
#include "ViewModels/PlayerViewModel.h"

#include <winrt/Microsoft.UI.Input.h>

namespace winrt::HaloDesktop::implementation
{
    PlayerOsd::PlayerOsd() : m_viewModel(winrt::make<PlayerViewModel>(App::Services()))
    {
    }
    winrt::HaloDesktop::PlayerViewModel PlayerOsd::ViewModel() const
    {
        return m_viewModel;
    }
    void PlayerOsd::TitleLabel(winrt::hstring const& value){FindName(L"TitleText").as<Microsoft::UI::Xaml::Controls::TextBlock>().Text(value);}
    void PlayerOsd::SourceLabel(winrt::hstring const& value)
    {
        FindName(L"SourceLabelText").as<Microsoft::UI::Xaml::Controls::TextBlock>().Text(value);
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
        if(auto list=FindName(L"AddonSubtitleList").try_as<Microsoft::UI::Xaml::Controls::ItemsControl>())list.ItemsSource(viewModel->AddonSubtitlesView());
        if (m_seekHandlersRegistered)
        {
            return;
        }

        auto const slider = FindName(L"SeekSlider").as<Microsoft::UI::Xaml::Controls::Slider>();
        slider.AddHandler(Microsoft::UI::Xaml::UIElement::PointerPressedEvent(),
                          winrt::box_value(Microsoft::UI::Xaml::Input::PointerEventHandler{
                              this, &PlayerOsd::OnSeekPointerPressed
                          }),
                          true);
        slider.AddHandler(Microsoft::UI::Xaml::UIElement::PointerMovedEvent(),
                          winrt::box_value(Microsoft::UI::Xaml::Input::PointerEventHandler{
                              this, &PlayerOsd::OnSeekPointerMoved
                          }),
                          true);
        slider.AddHandler(Microsoft::UI::Xaml::UIElement::PointerReleasedEvent(),
                          winrt::box_value(Microsoft::UI::Xaml::Input::PointerEventHandler{
                              this, &PlayerOsd::OnSeekPointerReleased
                          }),
                          true);
        auto const terminationHandler = winrt::box_value(Microsoft::UI::Xaml::Input::PointerEventHandler{
            this, &PlayerOsd::OnSeekPointerTerminated
        });
        slider.AddHandler(Microsoft::UI::Xaml::UIElement::PointerCaptureLostEvent(), terminationHandler, true);
        slider.AddHandler(Microsoft::UI::Xaml::UIElement::PointerCanceledEvent(), terminationHandler, true);
        m_seekHandlersRegistered = true;
    }
    void PlayerOsd::OnOsdPointerMoved(winrt::Windows::Foundation::IInspectable const& sender,
                                      Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        auto const pointerId = args.Pointer().PointerId();
        auto const position = args.GetCurrentPoint(sender.as<Microsoft::UI::Xaml::UIElement>()).Position();
        if (m_lastPointerPosition && m_lastPointerId == pointerId && m_lastPointerPosition->X == position.X &&
            m_lastPointerPosition->Y == position.Y)
        {
            return;
        }

        m_lastPointerId = pointerId;
        m_lastPointerPosition = position;
        m_viewModel.NotifyUserActivity();
    }
    void PlayerOsd::OnOsdWakePointerPressed(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        m_viewModel.NotifyUserActivity();
        // This layer exists only while the controls are hidden. Consume the first
        // press so it cannot activate an invisible control underneath.
        args.Handled(true);
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
    void PlayerOsd::OnSeekPointerPressed(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        if (m_seekPointerActive)
        {
            return;
        }

        m_seekPointerActive = true;
        m_viewModel.BeginScrub();
    }
    void PlayerOsd::OnSeekPointerMoved(winrt::Windows::Foundation::IInspectable const& sender,
                                       Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        auto const slider = sender.as<Microsoft::UI::Xaml::Controls::Slider>();
        auto const point = args.GetCurrentPoint(slider);
        if (!m_seekPointerActive && (point.IsInContact() || point.Properties().IsLeftButtonPressed()))
        {
            m_seekPointerActive = true;
            m_viewModel.BeginScrub();
        }
        if (m_seekPointerActive)
        {
            m_viewModel.ScrubTo(slider.Value());
        }
    }
    void PlayerOsd::OnSeekPointerReleased(
        winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        auto const slider = sender.as<Microsoft::UI::Xaml::Controls::Slider>();
        if (!m_seekPointerActive)
        {
            m_viewModel.BeginScrub();
        }
        m_seekPointerActive = false;
        m_viewModel.EndScrub(slider.Value());
    }
    void PlayerOsd::OnSeekPointerTerminated(
        winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        if (!m_seekPointerActive)
        {
            return;
        }

        m_seekPointerActive = false;
        m_viewModel.EndScrub(sender.as<Microsoft::UI::Xaml::Controls::Slider>().Value());
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
    void PlayerOsd::OnAddonSubtitleClick(winrt::Windows::Foundation::IInspectable const&sender,Microsoft::UI::Xaml::RoutedEventArgs const&){m_viewModel.SelectAddonSubtitle(winrt::unbox_value_or<winrt::hstring>(sender.as<Microsoft::UI::Xaml::Controls::Button>().Tag(),L""));}
    void PlayerOsd::OnSubtitleDelayDownClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                                             [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.AdjustSubtitleDelay(-50);
    }
    void PlayerOsd::OnSubtitleDelayUpClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                                           [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.AdjustSubtitleDelay(50);
    }
    void PlayerOsd::OnAudioDelayDownClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                                          [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.AdjustAudioDelay(-50);
    }
    void PlayerOsd::OnAudioDelayUpClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                                        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.AdjustAudioDelay(50);
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
    void PlayerOsd::OnSpeedOneThreeQuarterClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                                                [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.SetSpeed(1.75);
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
