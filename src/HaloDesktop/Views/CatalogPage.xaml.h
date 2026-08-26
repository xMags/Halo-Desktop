#pragma once

#include "CatalogPage.g.h"

#include <cstdint>

namespace winrt::HaloDesktop::implementation
{
    struct CatalogPage : CatalogPageT<CatalogPage>
    {
        CatalogPage();

        [[nodiscard]] winrt::HaloDesktop::CatalogViewModel ViewModel() const;
        void OnNavigatedTo(Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& args);
        void OnLoaded(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnPosterClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnUnloaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnGridLoaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnBackClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);

    private:
        void ApplyLayoutMetrics();
        winrt::HaloDesktop::CatalogViewModel m_viewModel{ nullptr };
        std::uint64_t m_metricsToken{};
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct CatalogPage : CatalogPageT<CatalogPage, implementation::CatalogPage>
    {
    };
}
