#pragma once

#include "SourcesPage.g.h"
#include "Services/Downloads/DownloadPageOperationState.h"

#include <string>
#include <winrt/Microsoft.UI.Xaml.Input.h>

namespace winrt::HaloDesktop::implementation
{
    // The sources sheet. Hosted in the window's sheet layer rather than the shell
    // frame, so the page it was opened from keeps rendering behind the scrim.
    // View behaviour only: keys, scrolling, dialogs and the clipboard; every rule
    // is the view model's.
    struct SourcesPage : SourcesPageT<SourcesPage>
    {
        SourcesPage();
        [[nodiscard]] winrt::HaloDesktop::SourcesViewModel ViewModel() const;
        void OnNavigatedTo(Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& args);
        void OnLoaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnUnloaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnExitCompleted(winrt::Windows::Foundation::IInspectable const&, winrt::Windows::Foundation::IInspectable const&);
        void OnScrollerSizeChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const&);

        void OnScrimTapped(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const&);
        void OnCloseClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnRetryClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnManageAddonsClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnPlayOfflineClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

        void OnAllFilterClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnUltraHdFilterClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnFullHdFilterClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnHdFilterClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnRecommendedSortClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnBestPictureSortClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSmallestFileSortClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnFastestStartSortClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

        void OnPickDetailsClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnPlayPickClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnPickSaveClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnRevealColdClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnRowPlayClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnRowChevronClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnRowFocused(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnDetailsSaveClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnDetailsCopyClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        void OnSheetPreviewKeyDown(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const&);
        void OnSheetKeyDown(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const&);
        void AttachKeyHandlers();
        void DetachKeyHandlers() noexcept;
        // Scrolls the selection into view only when it has left the viewport, so
        // arrowing through a visible group does not shunt the list under the cursor.
        void RevealSelection();
        void BeginClose();
        void SyncSelectors();

        winrt::fire_and_forget StartDownload(winrt::hstring key);
        [[nodiscard]] winrt::Windows::Foundation::IAsyncAction StartDownloadCore(
            winrt::hstring key,
            ::HaloDesktop::Services::Downloads::DownloadPageOperationState::Ticket ticket);
        [[nodiscard]] winrt::Windows::Foundation::IAsyncOperation<bool> ConfirmReplacementAsync();
        [[nodiscard]] winrt::Windows::Foundation::IAsyncAction ShowDownloadFailureAsync(std::wstring reason);
        winrt::fire_and_forget ShowOfflineMissingAsync();

        winrt::HaloDesktop::SourcesViewModel m_viewModel{ nullptr };
        ::HaloDesktop::Services::Downloads::DownloadPageOperationState m_downloadOperation;
        winrt::Windows::Foundation::IInspectable m_previewKeyDownHandler{ nullptr };
        winrt::Windows::Foundation::IInspectable m_keyDownHandler{ nullptr };
        bool m_closing{};
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct SourcesPage : SourcesPageT<SourcesPage, implementation::SourcesPage>
    {
    };
}
