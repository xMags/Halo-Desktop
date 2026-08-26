#pragma once

#include "ShellPage.g.h"

#include "Services/NavigationService.h"
#include "Services/ServiceInterfaces.h"

#include <array>
#include <winrt/HaloDesktop.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Navigation.h>
#include <winrt/Windows.Foundation.h>

namespace winrt::HaloDesktop::implementation
{
    struct ShellPage : ShellPageT<ShellPage>
    {
        ShellPage();

        void OnLoaded(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnUnloaded(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnItemInvoked(
            Microsoft::UI::Xaml::Controls::NavigationView const& sender,
            Microsoft::UI::Xaml::Controls::NavigationViewItemInvokedEventArgs const& args);
        void OnBackRequested(
            Microsoft::UI::Xaml::Controls::NavigationView const& sender,
            Microsoft::UI::Xaml::Controls::NavigationViewBackRequestedEventArgs const& args);
        void OnSearchAcceleratorInvoked(
            Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender,
            Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);
        void OnAccountClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnContentHostSizeChanged(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::SizeChangedEventArgs const& args);
        void OnPaneOpening(
            Microsoft::UI::Xaml::Controls::NavigationView const& sender,
            winrt::Windows::Foundation::IInspectable const& args);
        void OnPaneClosing(
            Microsoft::UI::Xaml::Controls::NavigationView const& sender,
            Microsoft::UI::Xaml::Controls::NavigationViewPaneClosingEventArgs const& args);

        void OnContentPointerPressed(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);

    private:
        void SetJumpBackVisibility(bool visible);
        [[nodiscard]] static bool IsWithinTextInput(Microsoft::UI::Xaml::DependencyObject const& element);

        [[nodiscard]] Microsoft::UI::Xaml::Controls::Frame ContentFrameControl() const;
        [[nodiscard]] Microsoft::UI::Xaml::Controls::InfoBadge DownloadsBadgeControl() const;
        [[nodiscard]] Microsoft::UI::Xaml::Controls::NavigationView NavigationControl() const;
        [[nodiscard]] Microsoft::UI::Xaml::Controls::NavigationViewItem NavigationItem(winrt::hstring const& name) const;
        void OnFrameNavigated(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& args);
        void NavigateFromTag(winrt::hstring const& tag);
        void UpdateNavigationState(::HaloDesktop::Services::Page page);
        void UpdateDownloadBadge();
        void RefreshJumpBackIn();
        winrt::fire_and_forget RefreshJumpBackInAsync();
        void RefreshAccountIdentity();

        bool m_attached{ false };
        bool m_paneOpen{ true };
        std::array<winrt::HaloDesktop::ContinueItem, 3> m_jumpItems{ nullptr, nullptr, nullptr };
        ::HaloDesktop::Services::DownloadChangedToken m_downloadChangedToken{};
        Microsoft::UI::Xaml::Controls::Frame::Navigated_revoker m_frameNavigatedRevoker{};
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct ShellPage : ShellPageT<ShellPage, implementation::ShellPage>
    {
    };
}
