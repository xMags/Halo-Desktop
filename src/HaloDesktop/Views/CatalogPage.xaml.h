#pragma once

#include "CatalogPage.g.h"

#include "Views/TitleMenu.h"

#include <cstdint>
#include <winrt/Microsoft.UI.Xaml.Input.h>

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
        // One handler for the page: every poster on it answers a right-click from here.
        void OnContextRequested(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const&);

    private:
        void ApplyLayoutMetrics();
        winrt::HaloDesktop::CatalogViewModel m_viewModel{ nullptr };
        ::HaloDesktop::Views::TitleMenu m_titleMenu;
        std::uint64_t m_metricsToken{};
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct CatalogPage : CatalogPageT<CatalogPage, implementation::CatalogPage>
    {
    };
}
