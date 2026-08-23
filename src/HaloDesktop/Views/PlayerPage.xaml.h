#pragma once

#include "PlayerPage.g.h"

#include <winrt/Windows.Foundation.h>

namespace winrt::HaloDesktop::implementation
{
    struct PlayerPage : PlayerPageT<PlayerPage>
    {
        PlayerPage();
        [[nodiscard]] winrt::HaloDesktop::PlayerViewModel ViewModel() const;
        void OnLoaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnUnloaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSpaceInvoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
                            Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const&);
        void OnLeftInvoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
                           Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const&);
        void OnRightInvoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
                            Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const&);
        void OnUpInvoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
                         Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const&);
        void OnDownInvoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
                           Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const&);
        void OnFullscreenInvoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
                                 Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const&);
        void OnEscapeInvoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
                             Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const&);

    private:
        void CompleteKeyboardAction(Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);
        winrt::HaloDesktop::PlayerViewModel m_viewModel{ nullptr };
    };
} // namespace winrt::HaloDesktop::implementation

namespace winrt::HaloDesktop::factory_implementation
{
    struct PlayerPage : PlayerPageT<PlayerPage, implementation::PlayerPage>
    {
    };
} // namespace winrt::HaloDesktop::factory_implementation
