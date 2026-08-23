#pragma once

#include "DetailPage.g.h"

namespace winrt::HaloDesktop::implementation
{
    struct DetailPage : DetailPageT<DetailPage>
    {
        DetailPage();
        [[nodiscard]] winrt::HaloDesktop::DetailViewModel ViewModel() const;
        void OnLoaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSeasonOneClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSeasonTwoClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnEpisodeClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnBrowseSourcesClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnResumeClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnDownloadsClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        winrt::HaloDesktop::DetailViewModel m_viewModel{ nullptr };
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct DetailPage : DetailPageT<DetailPage, implementation::DetailPage>
    {
    };
}
