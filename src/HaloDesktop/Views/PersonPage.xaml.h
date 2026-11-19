#pragma once

#include "PersonPage.g.h"

#include "Views/TitleMenu.h"

#include <cstdint>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>

namespace winrt::HaloDesktop::implementation
{
    // View behaviour only: sizes from the layout step, the role chips' checked
    // state, the header's wrap on a narrow window, the biography's Read more and
    // the back button's words.
    // Everything the page shows comes from PersonViewModel.
    struct PersonPage : PersonPageT<PersonPage>
    {
        PersonPage();

        [[nodiscard]] winrt::HaloDesktop::PersonViewModel ViewModel() const;
        void OnNavigatedTo(Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& args);
        void OnLoaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnUnloaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnBackClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnRetryClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnAllRolesClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnActingClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnDirectingClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnWritingClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnPosterClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnPersonHeadSizeChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const&);
        void OnBiographyTrimmedChanged(Microsoft::UI::Xaml::Controls::TextBlock const& sender, Microsoft::UI::Xaml::Controls::IsTextTrimmedChangedEventArgs const&);
        void OnReadMoreClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        // One handler for the page: every poster on it answers a right-click from here.
        void OnContextRequested(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const&);

    private:
        void ApplyLayoutMetrics();
        // Checks the chip the view model's filter names.
        void ApplyFilterChip();

        winrt::HaloDesktop::PersonViewModel m_viewModel{ nullptr };
        ::HaloDesktop::Views::TitleMenu m_titleMenu;
        Microsoft::UI::Xaml::Data::INotifyPropertyChanged::PropertyChanged_revoker m_viewModelChanged;
        std::uint64_t m_metricsToken{};
        bool m_listBound{};
        // Whether Read more has opened the biography past its four lines.
        bool m_biographyOpen{};
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct PersonPage : PersonPageT<PersonPage, implementation::PersonPage>
    {
    };
}
