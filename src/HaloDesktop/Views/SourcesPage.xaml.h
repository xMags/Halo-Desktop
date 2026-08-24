#pragma once

#include "SourcesPage.g.h"

namespace winrt::HaloDesktop::implementation
{
    struct SourcesPage : SourcesPageT<SourcesPage>
    {
        SourcesPage();
        [[nodiscard]] winrt::HaloDesktop::SourcesViewModel ViewModel() const;
        void OnNavigatedTo(Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& args);
        void OnLoaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnAllFilterClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnInstantFilterClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void On2160FilterClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void On1080FilterClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnRetryClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnPlayClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSourceRowClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSourceRowPointerEntered(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void OnSourceRowPointerExited(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void OnEditPlaybackClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnTeachingTipAction(Microsoft::UI::Xaml::Controls::TeachingTip const&, winrt::Windows::Foundation::IInspectable const&);
        void OnTeachingTipClosed(Microsoft::UI::Xaml::Controls::TeachingTip const&, Microsoft::UI::Xaml::Controls::TeachingTipClosedEventArgs const&);

    private:
        winrt::HaloDesktop::SourcesViewModel m_viewModel{ nullptr };
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct SourcesPage : SourcesPageT<SourcesPage, implementation::SourcesPage>
    {
    };
}
