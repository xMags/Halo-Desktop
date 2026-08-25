#pragma once

#include "CatalogPage.g.h"

namespace winrt::HaloDesktop::implementation
{
    struct CatalogPage : CatalogPageT<CatalogPage>
    {
        CatalogPage();

        [[nodiscard]] winrt::HaloDesktop::CatalogViewModel ViewModel() const;
        void OnNavigatedTo(Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& args);
        void OnPosterClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);

    private:
        winrt::HaloDesktop::CatalogViewModel m_viewModel{ nullptr };
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct CatalogPage : CatalogPageT<CatalogPage, implementation::CatalogPage>
    {
    };
}
