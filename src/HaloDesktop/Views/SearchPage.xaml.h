#pragma once

#include "SearchPage.g.h"

namespace winrt::HaloDesktop::implementation
{
    struct SearchPage : SearchPageT<SearchPage>
    {
        SearchPage();
        [[nodiscard]] winrt::HaloDesktop::SearchViewModel ViewModel() const;
        void OnLoaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnNavigatedTo(Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& args);
        void OnQuerySubmitted(Microsoft::UI::Xaml::Controls::AutoSuggestBox const&,
                              Microsoft::UI::Xaml::Controls::AutoSuggestBoxQuerySubmittedEventArgs const&);
        void OnAllFilterClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnMoviesFilterClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSeriesFilterClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnPeopleFilterClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnClearClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnTopMatchClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnShelfItemClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnRecentClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        winrt::HaloDesktop::SearchViewModel m_viewModel{ nullptr };
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct SearchPage : SearchPageT<SearchPage, implementation::SearchPage>
    {
    };
}
