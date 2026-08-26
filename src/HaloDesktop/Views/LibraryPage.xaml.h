#pragma once

#include "LibraryPage.g.h"

#include <cstdint>

namespace winrt::HaloDesktop::implementation
{
    struct LibraryPage : LibraryPageT<LibraryPage>
    {
        LibraryPage();
        [[nodiscard]] winrt::HaloDesktop::LibraryViewModel ViewModel() const;
        void OnLoaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnAllFilterClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnMoviesFilterClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSeriesFilterClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnRetryClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSortClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnPosterClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnUnloaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnGridLoaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        void ApplyLayoutMetrics();
        winrt::HaloDesktop::LibraryViewModel m_viewModel{ nullptr };
        std::uint64_t m_metricsToken{};
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct LibraryPage : LibraryPageT<LibraryPage, implementation::LibraryPage>
    {
    };
}
