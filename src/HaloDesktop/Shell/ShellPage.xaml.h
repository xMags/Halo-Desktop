#pragma once

#include "ShellPage.g.h"

#include "Services/NavigationService.h"
#include "Services/ServiceInterfaces.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <winrt/HaloDesktop.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Microsoft.UI.Xaml.Navigation.h>
#include <winrt/Windows.Foundation.h>

namespace winrt::HaloDesktop::implementation
{
    struct ShellPage : ShellPageT<ShellPage>
    {
        ShellPage();
        // Keeps the stock avatar to fall back on.
        void InitializeComponent();

        void OnLoaded(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnUnloaded(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnRailItemClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
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

        void OnContentPointerPressed(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void OnContentPointerEnded(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);

    private:
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
        [[nodiscard]] Microsoft::UI::Xaml::Controls::RadioButton CheckedRailItem();
        void MoveRailIndicator(Microsoft::UI::Xaml::Controls::RadioButton const& item);
        void OnFrameNavigated(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& args);
        void NavigateFromTag(winrt::hstring const& tag);
        void UpdateNavigationState(::HaloDesktop::Services::Page page);
        void UpdateDownloadBadge();
        void RefreshAccountIdentity();
        void UpdateAccountToolTip();
        winrt::fire_and_forget RefreshAccountPictureAsync();
        void ShowStockAvatar();
        void OnIdentityChanged();
        winrt::fire_and_forget ShowSkippedAddons(std::vector<std::wstring> names);

        bool m_attached{ false };
        // The account's name and tier, for the avatar's tooltip. UI thread only.
        winrt::hstring m_accountName;
        winrt::hstring m_accountRole;
        // The selection pill's running slide, stopped by the next one. UI thread only.
        Microsoft::UI::Xaml::Media::Animation::Storyboard m_indicatorSlide{ nullptr };
        // Advances with every picture request, so a slower earlier one never
        // replaces what a later one showed.
        std::uint64_t m_pictureRequest{};
        winrt::hstring m_shownPictureVersion;
        winrt::hstring m_shownPictureUser;
        Microsoft::UI::Xaml::Media::ImageSource m_stockAvatar{ nullptr };
        std::optional<std::uint32_t> m_nonTextPointerId;
        winrt::Windows::Foundation::IInspectable m_pointerPressedHandler{ nullptr };
        winrt::Windows::Foundation::IInspectable m_pointerEndedHandler{ nullptr };
        winrt::Windows::Foundation::IInspectable m_backPointerHandler{ nullptr };
        winrt::event_token m_gettingFocusToken{};
        ::HaloDesktop::Services::DownloadChangedToken m_downloadChangedToken{};
        ::HaloDesktop::Services::IdentityChangedToken m_identityChangedToken{};
        ::HaloDesktop::Services::IdentityChangedToken m_profileChangedToken{};
        std::uint64_t m_skippedAddonsToken{};
        // Skipped-addon names no dialog has shown yet, and whether the runner
        // showing them is going. UI thread only.
        std::vector<std::wstring> m_pendingSkippedAddons;
        bool m_skippedNoticeRunning{};
        Microsoft::UI::Xaml::Controls::Frame::Navigated_revoker m_frameNavigatedRevoker{};
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct ShellPage : ShellPageT<ShellPage, implementation::ShellPage>
    {
    };
}
