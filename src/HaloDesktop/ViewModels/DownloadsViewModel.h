#pragma once

#include "ChartBarViewModel.g.h"
#include "DownloadRowViewModel.g.h"
#include "DownloadsViewModel.g.h"
#include "Services/AppServices.h"
#include "Services/ServiceInterfaces.h"
#include "ViewModels/SourcePresentation.h"

#include <cstdint>
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
        [[nodiscard]] winrt::hstring SubsChip() const;
        [[nodiscard]] winrt::hstring Poster() const;
        [[nodiscard]] winrt::hstring RowArtwork() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility DownloadingVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility QueuedVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PausedVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility FailedVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility OnDiskVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility RetryVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ChooseSourceVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PauseVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ResumeVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility LeadNormalVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility LeadCautionVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility LeadCriticalVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ProgressAccentVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ProgressCautionVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ProgressCriticalVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SubsNormalVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SubsMutedVisibility() const noexcept;
        [[nodiscard]] winrt::hstring PauseGlyph() const;
        [[nodiscard]] winrt::hstring PauseLabel() const;
        [[nodiscard]] winrt::hstring DownloadedLine() const;
        [[nodiscard]] winrt::hstring FileName() const;
        [[nodiscard]] winrt::hstring AddedLabel() const;
        [[nodiscard]] winrt::hstring QualityBadgeTier() const;
        [[nodiscard]] winrt::hstring QualityBadgeDetail() const;
        [[nodiscard]] winrt::HaloDesktop::QualityBadgeTone QualityTone() const noexcept;
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
        [[nodiscard]] winrt::hstring ActionErrorText() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ActionErrorVisibility() const noexcept;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> TransfersView() const;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> ReadyView() const;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> ChartBarsView() const;
        [[nodiscard]] winrt::HaloDesktop::DownloadRowViewModel SelectedRow() const;
        [[nodiscard]] winrt::hstring RateText() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility RateNormalVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility RateIdleVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility RatePausedVisibility() const noexcept;
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
        [[nodiscard]] winrt::hstring SelectedPercentText() const;
        [[nodiscard]] winrt::hstring SelectedQualityLine() const;
        [[nodiscard]] winrt::hstring SelectedSize() const;
        [[nodiscard]] winrt::hstring SelectedSizeFactLabel() const;
        [[nodiscard]] winrt::hstring SelectedSubs() const;
        [[nodiscard]] winrt::hstring SelectedAdded() const;
        [[nodiscard]] winrt::hstring SelectedFileName() const;
        [[nodiscard]] winrt::hstring SelectedPoster() const;
        [[nodiscard]] winrt::hstring ReadyActionLabel() const;
        [[nodiscard]] winrt::hstring PaneNote() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PaneNoteVisibility() const noexcept;
        [[nodiscard]] winrt::hstring StorageLine() const;
        [[nodiscard]] winrt::hstring FreeLine() const;
        [[nodiscard]] winrt::hstring StoredLine() const;
        [[nodiscard]] winrt::hstring InFlightLine() const;
        [[nodiscard]] winrt::hstring PeakText() const;
        [[nodiscard]] double StorageFraction() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility DetailVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility FolderVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SelectedTransferVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SelectedReadyVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PauseVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ResumeVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ChooseSourceVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility TransferSectionVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ReadySectionVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility EmptyVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility NoMatchesVisibility() const noexcept;
        [[nodiscard]] winrt::hstring NoMatchesLine() const;
        [[nodiscard]] winrt::hstring FilterAllCount() const;
        [[nodiscard]] winrt::hstring FilterActiveCount() const;
        [[nodiscard]] winrt::hstring FilterReadyCount() const;
        [[nodiscard]] winrt::hstring FilterFailedCount() const;
        [[nodiscard]] std::int32_t FilterIndex() const noexcept;
        [[nodiscard]] double StoredFraction() const noexcept;
        [[nodiscard]] double InFlightFraction() const noexcept;
        [[nodiscard]] winrt::hstring DownloadDirectory() const;
        [[nodiscard]] winrt::hstring FolderLine() const;
        void Select(winrt::hstring const& id);
        void SetFilter(std::int32_t index);
        void Pause(winrt::hstring const& id);
        void Resume(winrt::hstring const& id);
        void Retry(winrt::hstring const& id);
        void Cancel(winrt::hstring const& id);
        void ChooseSourceFor(winrt::hstring const& id);
        void OpenDownloadFolder();
        void RetryFailed();
        void PauseAll();
        void ResumeAll();
        void PauseSelected();
        void ResumeSelected();
        void CancelSelected();
        void DeleteSelected();
        void OpenPlayer();
        void ChooseSource();
        void BrowseLibrary();
        [[nodiscard]] winrt::Windows::Foundation::IAsyncAction SetDownloadDirectoryAsync(
            std::filesystem::path directory);
        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        void Synchronize();
        void SynchronizeRows(
            winrt::Windows::Foundation::Collections::IObservableVector<winrt::HaloDesktop::DownloadItem> const& source,
            winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> const& target);
        void RebuildFilteredViews();
        void RebuildChart();
        void ResolveSelection();
        void RaiseState();
        [[nodiscard]] winrt::HaloDesktop::DownloadItem SelectedItem() const;
        [[nodiscard]] bool SelectedIsReady() const noexcept;

        std::shared_ptr<::HaloDesktop::Services::IDownloadService> m_downloads;
        std::shared_ptr<::HaloDesktop::Services::NavigationService> m_navigation;
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_transfers{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_ready{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_filteredTransfers{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_filteredReady{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_chartBars{ nullptr };
        std::int32_t m_filterIndex{};
        winrt::HaloDesktop::DownloadRowViewModel m_selected{ nullptr };
        winrt::hstring m_selectedId;
        ::HaloDesktop::Services::DownloadChangedToken m_changedToken{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
