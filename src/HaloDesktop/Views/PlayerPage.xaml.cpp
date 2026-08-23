#include "pch.h"
#include "Views/PlayerPage.xaml.h"
#if __has_include("PlayerPage.g.cpp")
#include "PlayerPage.g.cpp"
#endif

#include "Controls/PlayerOsd.xaml.h"
#include "ViewModels/PlayerViewModel.h"

namespace winrt::HaloDesktop::implementation
{
    PlayerPage::PlayerPage() = default;
    winrt::HaloDesktop::PlayerViewModel PlayerPage::ViewModel() const
    {
        return m_viewModel;
    }
    void PlayerPage::OnLoaded([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                              [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel = FindName(L"PlayerOverlay").as<winrt::HaloDesktop::PlayerOsd>().ViewModel();
        winrt::get_self<PlayerViewModel>(m_viewModel)->Activate();
    }
    void PlayerPage::OnUnloaded([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                                [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (m_viewModel)
        {
            winrt::get_self<PlayerViewModel>(m_viewModel)->Deactivate();
        }
    }
    void PlayerPage::OnSpaceInvoked([[maybe_unused]] Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
                                    Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        m_viewModel.TogglePause();
        CompleteKeyboardAction(args);
    }
    void PlayerPage::OnLeftInvoked([[maybe_unused]] Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
                                   Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        m_viewModel.SeekRelative(-10.0);
        CompleteKeyboardAction(args);
    }
    void PlayerPage::OnRightInvoked([[maybe_unused]] Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
                                    Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        m_viewModel.SeekRelative(10.0);
        CompleteKeyboardAction(args);
    }
    void PlayerPage::OnUpInvoked([[maybe_unused]] Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
                                 Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        m_viewModel.ChangeVolume(5.0);
        CompleteKeyboardAction(args);
    }
    void PlayerPage::OnDownInvoked([[maybe_unused]] Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
                                   Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        m_viewModel.ChangeVolume(-5.0);
        CompleteKeyboardAction(args);
    }
    void PlayerPage::OnFullscreenInvoked([[maybe_unused]] Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
                                         Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        m_viewModel.ToggleFullscreen();
        CompleteKeyboardAction(args);
    }
    void PlayerPage::OnEscapeInvoked([[maybe_unused]] Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
                                     Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        m_viewModel.HandleEscape();
        CompleteKeyboardAction(args);
    }
    void PlayerPage::CompleteKeyboardAction(Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        if (m_viewModel)
        {
            m_viewModel.NotifyUserActivity();
        }
        args.Handled(true);
    }
} // namespace winrt::HaloDesktop::implementation
