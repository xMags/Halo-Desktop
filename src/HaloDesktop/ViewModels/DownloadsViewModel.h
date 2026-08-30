#pragma once

#include "ChartBarViewModel.g.h"
#include "DownloadRowViewModel.g.h"
#include "DownloadsViewModel.g.h"
#include "Services/AppServices.h"
#include "Services/ServiceInterfaces.h"

#include <memory>
#include <winrt/Microsoft.UI.Xaml.Data.h>

namespace winrt::HaloDesktop::implementation
{
    struct DownloadRowViewModel : DownloadRowViewModelT<DownloadRowViewModel>
    {
        explicit DownloadRowViewModel(winrt::HaloDesktop::DownloadItem item);
        void Update(winrt::HaloDesktop::DownloadItem item);
        [[nodiscard]] winrt::HaloDesktop::DownloadItem Item() const;
        [[nodiscard]] winrt::hstring Id() const;
        [[nodiscard]] winrt::hstring Tag() const;
        [[nodiscard]] winrt::hstring Name() const;
        [[nodiscard]] winrt::hstring Sub() const;
        [[nodiscard]] winrt::hstring StateLabel() const;
        [[nodiscard]] double Progress() const noexcept;
        [[nodiscard]] winrt::hstring Detail() const;
        [[nodiscard]] winrt::hstring QualityLine() const;
        [[nodiscard]] winrt::hstring Size() const;
        [[nodiscard]] winrt::hstring Subs() const;
        [[nodiscard]] winrt::hstring Poster() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility DownloadingVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility QueuedVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PausedVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility FailedVisibility() const noexcept;
        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        void RaiseState();
        winrt::HaloDesktop::DownloadItem m_item{ nullptr };
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };

    struct ChartBarViewModel : ChartBarViewModelT<ChartBarViewModel>
    {
        ChartBarViewModel(double value, bool recent);
        [[nodiscard]] double Height() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility RecentVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility HistoricalVisibility() const noexcept;

    private:
        double m_height{};
        bool m_recent{};
    };

    struct DownloadsViewModel : DownloadsViewModelT<DownloadsViewModel>
    {
        explicit DownloadsViewModel(::HaloDesktop::Services::AppServices const& services);
        ~DownloadsViewModel();
        void Activate();
        void Deactivate() noexcept;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable Transfers() const;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable Ready() const;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable ChartBars() const;
        [[nodiscard]] winrt::hstring InfoTitle() const;
        [[nodiscard]] winrt::hstring InfoMessage() const;
        [[nodiscard]] winrt::hstring ActionErrorText() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ActionErrorVisibility() const noexcept;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> TransfersView() const;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> ReadyView() const;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> ChartBarsView() const;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable SelectedRow() const;
        [[nodiscard]] winrt::hstring RateText() const;
        [[nodiscard]] winrt::hstring QueueLine() const;
        [[nodiscard]] winrt::hstring TransferCountLabel() const;
        [[nodiscard]] winrt::hstring ReadyCountLabel() const;
        [[nodiscard]] winrt::hstring PauseAllLabel() const;
        [[nodiscard]] winrt::hstring PauseAllGlyph() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PauseAllVisibility() const noexcept;
        [[nodiscard]] bool IsPausedAll() const noexcept;
        [[nodiscard]] winrt::hstring SelectedTag() const;
        [[nodiscard]] winrt::hstring SelectedTitle() const;
        [[nodiscard]] winrt::hstring SelectedSub() const;
        [[nodiscard]] double SelectedProgress() const noexcept;
        [[nodiscard]] winrt::hstring SelectedDetail() const;
        [[nodiscard]] winrt::hstring SelectedQualityLine() const;
        [[nodiscard]] winrt::hstring SelectedSize() const;
        [[nodiscard]] winrt::hstring SelectedSubs() const;
        [[nodiscard]] winrt::hstring SelectedPoster() const;
        [[nodiscard]] winrt::hstring ReadyActionLabel() const;
        [[nodiscard]] winrt::hstring StorageLine() const;
        [[nodiscard]] winrt::hstring FreeLine() const;
        [[nodiscard]] winrt::hstring StoredLine() const;
        [[nodiscard]] winrt::hstring InFlightLine() const;
        [[nodiscard]] winrt::hstring PeakText() const;
        [[nodiscard]] double StorageFraction() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility DetailVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SelectedTransferVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SelectedReadyVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PauseVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ResumeVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ChooseSourceVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility TransferSectionVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ReadySectionVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility EmptyVisibility() const noexcept;
        void Select(winrt::hstring const& id);
        void PauseAll();
        void ResumeAll();
        void PauseSelected();
        void ResumeSelected();
        void CancelSelected();
        void DeleteSelected();
        void OpenPlayer();
        void ChooseSource();
        [[nodiscard]] winrt::Windows::Foundation::IAsyncAction SetDownloadDirectoryAsync(
            std::filesystem::path directory);
        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        void Synchronize();
        void SynchronizeRows(
            winrt::Windows::Foundation::Collections::IObservableVector<winrt::HaloDesktop::DownloadItem> const& source,
            winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> const& target);
        void RebuildChart();
        void ResolveSelection();
        void RaiseState();
        [[nodiscard]] winrt::HaloDesktop::DownloadItem SelectedItem() const;
        [[nodiscard]] bool SelectedIsReady() const noexcept;

        std::shared_ptr<::HaloDesktop::Services::IDownloadService> m_downloads;
        std::shared_ptr<::HaloDesktop::Services::NavigationService> m_navigation;
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_transfers{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_ready{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_chartBars{ nullptr };
        winrt::HaloDesktop::DownloadRowViewModel m_selected{ nullptr };
        winrt::hstring m_selectedId;
        ::HaloDesktop::Services::DownloadChangedToken m_changedToken{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
