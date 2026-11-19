#pragma once

#include "DownloadsPage.g.h"

#include "Views/DownloadsChartPresenter.h"

#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Windows.Foundation.h>

namespace winrt::HaloDesktop::implementation
{
    struct DownloadsPage : DownloadsPageT<DownloadsPage>
    {
        DownloadsPage();
        [[nodiscard]] winrt::HaloDesktop::DownloadsViewModel ViewModel() const;
        void OnLoaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnUnloaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnFilterSelectionChanged(Microsoft::UI::Xaml::Controls::SelectorBar const&, Microsoft::UI::Xaml::Controls::SelectorBarSelectionChangedEventArgs const&);
        void OnStatusToggleClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnWaitingToggleClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnToggleDetailsClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnCardPlayRequested(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnReadyPlayClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnReadyRevealClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnReadyDeleteClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnPauseAllClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnPauseRowClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnResumeRowClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnRetryRowClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnChooseSourceRowClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnCancelRowClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnManageFolderClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnOpenFolderClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnDeleteAllClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnRetryFailedClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnBrowseLibraryClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        void SyncFilterSelection();
        void UpdateFilterTexts();
        void ApplyChart();
        // The status card's and the queue card's open state is the page's own:
        // it is how the viewer left this page, not anything about the downloads.
        void ApplyFoldedCards();
        winrt::fire_and_forget ShowPauseAllDialog();
        winrt::fire_and_forget ShowCancelDialog(winrt::hstring id);
        winrt::fire_and_forget ShowDeleteDialog();
        winrt::fire_and_forget ShowDeleteAllDialog();
        winrt::fire_and_forget PickDownloadFolder();

        winrt::HaloDesktop::DownloadsViewModel m_viewModel{ nullptr };
        ::HaloDesktop::Views::DownloadsChartPresenter m_chart;
        winrt::event_token m_viewModelChangedToken{};
        bool m_statusOpen{};
        bool m_waitingOpen{};
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct DownloadsPage : DownloadsPageT<DownloadsPage, implementation::DownloadsPage> {};
}
