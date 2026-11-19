#pragma once

#include "LibraryPage.g.h"

#include "Controls/SkeletonPulse.h"
#include "Views/TitleMenu.h"

#include <cstdint>
#include <memory>
#include <winrt/Microsoft.UI.Xaml.Input.h>

namespace winrt::HaloDesktop::implementation
{
    struct LibraryPage : LibraryPageT<LibraryPage>
    {
        LibraryPage();
        [[nodiscard]] winrt::HaloDesktop::LibraryViewModel ViewModel() const;
        void OnLoaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnFilterSelectionChanged(Microsoft::UI::Xaml::Controls::SelectorBar const&, Microsoft::UI::Xaml::Controls::SelectorBarSelectionChangedEventArgs const&);
        void OnRetryClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSortClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnPosterClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnUnloaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        // One handler for the page: every poster on it answers a right-click from here.
        void OnContextRequested(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const&);

    private:
        void ApplyLayoutMetrics();
        void EnsureSkeletonPulse();
        void UpdateSkeletonPulse();
        winrt::HaloDesktop::LibraryViewModel m_viewModel{ nullptr };
        ::HaloDesktop::Views::TitleMenu m_titleMenu;
        std::unique_ptr<::HaloDesktop::Controls::SkeletonPulse> m_skeletonPulse;
        Microsoft::UI::Xaml::Data::INotifyPropertyChanged::PropertyChanged_revoker m_viewModelChanged;
        std::uint64_t m_metricsToken{};
        bool m_listBound{};
        bool m_onScreen{};
        // Whether the grid was last seen shown, so it fades in as it takes over from its placeholders.
        bool m_gridShown{};
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct LibraryPage : LibraryPageT<LibraryPage, implementation::LibraryPage>
    {
    };
}
