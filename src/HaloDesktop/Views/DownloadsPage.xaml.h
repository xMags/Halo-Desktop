#pragma once

#include "DownloadsPage.g.h"

#include <winrt/Windows.Foundation.h>

namespace winrt::HaloDesktop::implementation
{
    struct DownloadsPage : DownloadsPageT<DownloadsPage>
    {
        DownloadsPage();
        [[nodiscard]] winrt::HaloDesktop::DownloadsViewModel ViewModel() const;
        void OnLoaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnUnloaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnTransferItemClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::ItemClickEventArgs const&);
        void OnReadyItemClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::ItemClickEventArgs const&);
        void OnPauseAllClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnPauseSelectedClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnResumeSelectedClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnStartNowClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnCancelSelectedClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnDeleteSelectedClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnPlaySelectedClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnManageFolderClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        winrt::fire_and_forget ShowPauseAllDialog();
        winrt::fire_and_forget ShowCancelDialog();
        winrt::fire_and_forget ShowDeleteDialog();

        winrt::HaloDesktop::DownloadsViewModel m_viewModel{ nullptr };
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct DownloadsPage : DownloadsPageT<DownloadsPage, implementation::DownloadsPage> {};
}
