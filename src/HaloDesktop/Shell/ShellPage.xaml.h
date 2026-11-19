#pragma once

#include "ShellPage.g.h"

#include "Services/NavigationService.h"
#include "Services/ServiceInterfaces.h"
#include "Views/TitleMenu.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <winrt/HaloDesktop.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Navigation.h>
#include <winrt/Windows.Foundation.h>

namespace winrt::HaloDesktop::implementation
{
    struct ShellPage : ShellPageT<ShellPage>
    {
        ShellPage();
        // Restores the pane the way it was left before the first layout, so a
        // launch never animates it open or closed.
        void InitializeComponent();

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
        void OnBackAcceleratorInvoked(
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
        void OnContentPointerEnded(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        // A Jump back in row opens the same menu as the continue card it mirrors.
        void OnJumpContextRequested(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const& args);

    private:
        void SetJumpBackVisibility(bool visible);
        [[nodiscard]] static Microsoft::UI::Xaml::DependencyObject TextInputRoot(Microsoft::UI::Xaml::DependencyObject const& element);
        [[nodiscard]] static bool IsWithinTextInput(Microsoft::UI::Xaml::DependencyObject const& element);
        [[nodiscard]] static bool KeepsFocusOnInteraction(Microsoft::UI::Xaml::DependencyObject const& element);
        [[nodiscard]] bool IsInShellContent(Microsoft::UI::Xaml::DependencyObject const& element);
        void AttachPointerHandlers();
        void DetachPointerHandlers() noexcept;
        void OnShellPointerPressed(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);

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
        void UpdateAccountToolTip();
        winrt::fire_and_forget RefreshAccountPictureAsync();
        void ShowStockAvatar();
        void OnIdentityChanged();
        winrt::fire_and_forget ShowSkippedAddons(std::vector<std::wstring> names);
        void ApplyPaneState(bool open);
        void UpdatePaneToggleOffset();
        void RememberPaneState(bool open);
        winrt::fire_and_forget WritePaneStateAsync();

        bool m_attached{ false };
        bool m_paneOpen{};
        // The pane state last handed to the preference store, and whether a write
        // of it is running. Both are touched on the UI thread only.
        bool m_storedPaneOpen{};
        bool m_paneWriteRunning{};
        // Advances with every picture request, so a slower earlier one never
        // replaces what a later one showed.
        std::uint64_t m_pictureRequest{};
        winrt::hstring m_shownPictureVersion;
        winrt::hstring m_shownPictureUser;
        Microsoft::UI::Xaml::Media::ImageSource m_stockAvatar{ nullptr };
        // Part of the NavigationView's template, found once it has loaded.
        Microsoft::UI::Xaml::FrameworkElement m_paneToggle{ nullptr };
        std::optional<std::uint32_t> m_nonTextPointerId;
        winrt::Windows::Foundation::IInspectable m_pointerPressedHandler{ nullptr };
        winrt::Windows::Foundation::IInspectable m_pointerEndedHandler{ nullptr };
        winrt::Windows::Foundation::IInspectable m_backPointerHandler{ nullptr };
        winrt::event_token m_gettingFocusToken{};
        std::array<winrt::HaloDesktop::ContinueItem, 3> m_jumpItems{ nullptr, nullptr, nullptr };
        ::HaloDesktop::Services::DownloadChangedToken m_downloadChangedToken{};
        ::HaloDesktop::Services::CatalogChangedToken m_continueChangedToken{};
        ::HaloDesktop::Services::CatalogChangedToken m_userStateChangedToken{};
        ::HaloDesktop::Services::IdentityChangedToken m_identityChangedToken{};
        ::HaloDesktop::Services::IdentityChangedToken m_profileChangedToken{};
        std::uint64_t m_skippedAddonsToken{};
        // Skipped-addon names no dialog has shown yet, and whether the runner
        // showing them is going. UI thread only.
        std::vector<std::wstring> m_pendingSkippedAddons;
        bool m_skippedNoticeRunning{};
        ::HaloDesktop::Views::TitleMenu m_titleMenu;
        Microsoft::UI::Xaml::Controls::Frame::Navigated_revoker m_frameNavigatedRevoker{};
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct ShellPage : ShellPageT<ShellPage, implementation::ShellPage>
    {
    };
}
