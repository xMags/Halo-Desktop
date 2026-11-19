#pragma once

#include "DetailPage.g.h"

#include "Views/TitleMenu.h"

#include <memory>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>

namespace HaloDesktop::Controls
{
    class SkeletonPulse;
}

namespace HaloDesktop::Views
{
    class DetailBackdropPresenter;
}

namespace winrt::HaloDesktop::implementation
{
    // The title page. View behaviour only: responsive metrics from the layout
    // policy, the art behind the page, the compact bar, the skeleton's pulse and
    // the right-click menus. Everything it shows comes from DetailViewModel.
    struct DetailPage : DetailPageT<DetailPage>
    {
        DetailPage();
        ~DetailPage();
        void InitializeComponent();

        [[nodiscard]] winrt::HaloDesktop::DetailViewModel ViewModel() const;
        void OnNavigatedTo(Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& args);
        void OnLoaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnUnloaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSizeChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const&);
        void OnPageScrolled(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::ScrollViewerViewChangedEventArgs const&);
        void OnBackClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnRetryClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnPlayClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnCompactLogoFailed(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::ExceptionRoutedEventArgs const&);
        // Episode rows and "More like this" posters answer a right-click from here.
        void OnContextRequested(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const&);

    private:
        void OnViewModelChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Data::PropertyChangedEventArgs const& args);
        void ApplyLayout();
        void UpdateCompactBar();
        void ShowArt();
        void ShowCompactLogo();
        void UpdateSkeletonPulse();

        winrt::HaloDesktop::DetailViewModel m_viewModel{ nullptr };
        ::HaloDesktop::Views::TitleMenu m_titleMenu;
        std::unique_ptr<::HaloDesktop::Views::DetailBackdropPresenter> m_backdrop;
        std::unique_ptr<::HaloDesktop::Controls::SkeletonPulse> m_skeletonPulse;
        winrt::hstring m_blurredUrl;
        winrt::hstring m_compactLogoUrl;
        bool m_compactShown{};
        bool m_contentShown{};
        bool m_onScreen{};
        bool m_pulseUnavailable{};
        winrt::Microsoft::UI::Xaml::Data::INotifyPropertyChanged::PropertyChanged_revoker m_viewModelChanged;
        // The hero's Play button settling into its place, so the bar is judged
        // again once it has a real position rather than only on scroll.
        winrt::Microsoft::UI::Xaml::FrameworkElement::SizeChanged_revoker m_playButtonSized;
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct DetailPage : DetailPageT<DetailPage, implementation::DetailPage>
    {
    };
}
