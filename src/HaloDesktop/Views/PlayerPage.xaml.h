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
        void OnPlayerSizeChanged(winrt::Windows::Foundation::IInspectable const&,
                                 Microsoft::UI::Xaml::SizeChangedEventArgs const&);
        void OnOpenFileClick(winrt::Windows::Foundation::IInspectable const&,
                             Microsoft::UI::Xaml::RoutedEventArgs const&);
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
        void OnOverlayPreviewKeyDown(winrt::Windows::Foundation::IInspectable const&,
                                     Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const&);

    private:
        void OpenRememberedFileOrPrompt();
        winrt::fire_and_forget OpenFilePicker();
        void OpenSource(winrt::hstring const& path);
        void ShowMediaPrompt(winrt::hstring const& message);
        void UpdateOverlayLayout();
        void CompleteKeyboardAction(Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);

        winrt::HaloDesktop::PlayerViewModel m_viewModel{ nullptr };
        bool m_loaded{};
        bool m_pickerOpen{};
    };
} // namespace winrt::HaloDesktop::implementation

namespace winrt::HaloDesktop::factory_implementation
{
    struct PlayerPage : PlayerPageT<PlayerPage, implementation::PlayerPage>
    {
    };
} // namespace winrt::HaloDesktop::factory_implementation
