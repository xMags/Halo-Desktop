#pragma once

#include "SettingsPage.g.h"
#include "Services/AddonRowPolicy.h"

#include <cstddef>
#include <cstdint>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.h>

namespace winrt::HaloDesktop::implementation
{
    struct SettingsPage : SettingsPageT<SettingsPage>
    {
        SettingsPage();
        [[nodiscard]] winrt::HaloDesktop::SettingsViewModel ViewModel() const;
        [[nodiscard]] winrt::HaloDesktop::TmdbKeyViewModel TmdbKey() const { return m_tmdbKey; }
        void OnSaveTmdbKeyClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnRemoveTmdbKeyClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnTmdbKeyKeyDown(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const&);
        [[nodiscard]] static winrt::hstring OnOffText(bool isOn);
        void OnLoaded(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnUnloaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnAppearanceRailClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnLightThemeClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnDarkThemeClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSystemThemeClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnAddonsRailClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnPlaybackRailClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSubtitlesRailClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnAccountRailClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnAddAddonClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnAddonUrlKeyDown(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);
        void OnRetrySettingsClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnDeleteAddonClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnInstallForEveryoneClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnAddonDragStarting(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::DragItemsStartingEventArgs const& args);
        void OnAddonDragCompleted(Microsoft::UI::Xaml::Controls::ListViewBase const& sender, Microsoft::UI::Xaml::Controls::DragItemsCompletedEventArgs const& args);
        void OnAddonRowContextRequested(Microsoft::UI::Xaml::UIElement const& sender, Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const& args);
        void OnDefaultFontClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSystemFontClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSerifFontClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnMonoFontClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnNoOutlineClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnThinOutlineClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnNormalOutlineClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnHeavyOutlineClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSignOutClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnSignInToSyncClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnCheckForUpdatesClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnFormViewChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Windows::Foundation::IInspectable const&);
        void OnFormSizeChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const&);

    private:
        void ScrollTo(wchar_t const* elementName);
        void UpdateRailSelection();
        winrt::fire_and_forget ShowDeleteAddonDialog(winrt::hstring id);
        winrt::fire_and_forget ShowInstallForEveryoneDialog(winrt::hstring id);
        winrt::fire_and_forget ShowSignOutDialog();
        winrt::fire_and_forget OpenReleasesPageAsync();
        winrt::fire_and_forget RefreshAccountPictureAsync();
        void ShowStockAccountPicture();
        [[nodiscard]] winrt::Microsoft::UI::Xaml::Controls::ContentDialog MakeConfirmation(
            ::HaloDesktop::Services::ConfirmationCopy const& copy);
        winrt::HaloDesktop::SettingsViewModel m_viewModel{ nullptr };
        winrt::HaloDesktop::TmdbKeyViewModel m_tmdbKey{ nullptr };
        winrt::Microsoft::UI::Xaml::Data::INotifyPropertyChanged::PropertyChanged_revoker m_viewModelChangedRevoker{};
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_healthTimer{ nullptr };
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer::Tick_revoker m_healthTickRevoker{};
        winrt::Microsoft::UI::Xaml::Media::ImageSource m_stockAccountPicture{ nullptr };
        // The picture on screen and the request that may replace it; a reply
        // to an older request, or for another account, is dropped.
        winrt::hstring m_shownPictureVersion;
        winrt::hstring m_shownPictureUser;
        std::uint64_t m_pictureRequest{};
        bool m_loaded{};
        // Index into the rail order, held so a scroll only restyles the rail when the
        // section under the anchor actually changes.
        std::size_t m_activeRail{ 0 };
        bool m_railApplied{};
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct SettingsPage : SettingsPageT<SettingsPage, implementation::SettingsPage>
    {
    };
}
