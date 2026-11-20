#pragma once

#include "DownloadRowViewModel.g.h"
#include "DownloadsViewModel.g.h"
#include "Services/AppServices.h"
#include "Services/ServiceInterfaces.h"
#include "ViewModels/DownloadsPresentationPolicy.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>
#include <winrt/Microsoft.UI.Xaml.Data.h>

namespace winrt::HaloDesktop::implementation
{
    // A download's row, card and pane projection over one DownloadRow. The row
    // is handed a rebuilt projection on every progress tick, four times a
    // second per transfer; raising every property each time would re-run every
    // binding and re-decode the artwork, so only what changed is announced.
    struct DownloadRowViewModel : DownloadRowViewModelT<DownloadRowViewModel>
    {
        explicit DownloadRowViewModel(::HaloDesktop::ViewModels::DownloadsPresentation::DownloadRow row);

        void Update(::HaloDesktop::ViewModels::DownloadsPresentation::DownloadRow row);
        void SetSelected(bool selected);
        void SetTopRule(bool visible);
        [[nodiscard]] ::HaloDesktop::ViewModels::DownloadsPresentation::DownloadRow const& Row() const noexcept;

        [[nodiscard]] winrt::hstring Id() const;
        [[nodiscard]] winrt::hstring Tag() const;
        [[nodiscard]] winrt::hstring EpisodeLabel() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility EpisodeLabelVisibility() const noexcept;
        [[nodiscard]] winrt::hstring Name() const;
        [[nodiscard]] winrt::hstring Sub() const;
        [[nodiscard]] winrt::hstring AccessibleName() const;
        [[nodiscard]] winrt::hstring MetaId() const;
        [[nodiscard]] winrt::hstring RowArtwork() const;
        [[nodiscard]] winrt::hstring SavedArtwork() const;
        [[nodiscard]] winrt::hstring SavedLogo() const;
        [[nodiscard]] winrt::hstring OnlineLogo() const;
        [[nodiscard]] double Progress() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::GridLength ProgressDone() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::GridLength ProgressRest() const noexcept;
        [[nodiscard]] winrt::hstring PercentText() const;
        [[nodiscard]] winrt::hstring Lead() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ToneNormalVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ToneCautionVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ToneCriticalVisibility() const noexcept;
        [[nodiscard]] winrt::hstring DownloadedLine() const;
        [[nodiscard]] winrt::hstring MetaLead() const;
        [[nodiscard]] winrt::hstring MetaMiddle() const;
        [[nodiscard]] winrt::hstring MetaLast() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility UpNextVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility TransferBodyVisibility() const noexcept;
        [[nodiscard]] double ThumbOpacity() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ChooseSourceVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PauseVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ResumeVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility RetryVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PanePauseVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PaneResumeVisibility() const noexcept;
        [[nodiscard]] winrt::hstring StatePill() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility StateAccentVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility StateCautionVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility StateCriticalVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility StateNeutralVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility FailedNoteVisibility() const noexcept;
        [[nodiscard]] winrt::hstring PaneMeta() const;
        [[nodiscard]] winrt::hstring QualityLine() const;
        [[nodiscard]] winrt::hstring Subs() const;
        [[nodiscard]] winrt::hstring AddedFact() const;
        [[nodiscard]] winrt::hstring SizeFactLabel() const;
        [[nodiscard]] winrt::hstring FileName() const;
        [[nodiscard]] winrt::hstring CardTitle() const;
        [[nodiscard]] winrt::hstring CardSub() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility CardSubVisibility() const noexcept;
        [[nodiscard]] winrt::hstring CardMeta() const;
        [[nodiscard]] winrt::hstring QualityBadge() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility QualityBadgeGoldVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility QualityBadgePlainVisibility() const noexcept;
        [[nodiscard]] bool IsSelected() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SelectedVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility TopRuleVisibility() const noexcept;
        [[nodiscard]] bool IsPaused() const noexcept;
        [[nodiscard]] bool IsExpanded() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility DetailsVisibility() const noexcept;
        [[nodiscard]] float ChevronRotation() const noexcept;
        void SetExpanded(bool expanded);

        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        void Raise(wchar_t const* property);

        ::HaloDesktop::ViewModels::DownloadsPresentation::DownloadRow m_row;
        bool m_selected{};
        bool m_topRule{};
        bool m_expanded{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };

    struct DownloadsViewModel : DownloadsViewModelT<DownloadsViewModel>
    {
        explicit DownloadsViewModel(::HaloDesktop::Services::AppServices const& services);
        ~DownloadsViewModel();
        void Activate();
        void Deactivate() noexcept;

        // The unfinished transfers the filter shows, by section (SectionOf).
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> RunningView() const;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> WaitingView() const;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> AttentionView() const;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> ReadyView() const;
        // The 28 bars of the last minute's throughput, oldest first. Raised as
        // "ChartBars" only when a new sample changes them.
        [[nodiscard]] std::vector<::HaloDesktop::ViewModels::DownloadsPresentation::ChartBar> const& ChartBars() const noexcept;

        [[nodiscard]] winrt::hstring ActionErrorText() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ActionErrorVisibility() const noexcept;
        [[nodiscard]] winrt::hstring SummaryLine() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SummaryVisibility() const noexcept;
        [[nodiscard]] winrt::hstring FilterAllCount() const;
        [[nodiscard]] bool CanDeleteAll() const;
        [[nodiscard]] winrt::hstring FilterActiveCount() const;
        [[nodiscard]] winrt::hstring FilterReadyCount() const;
        [[nodiscard]] winrt::hstring FilterFailedCount() const;
        [[nodiscard]] std::int32_t FilterIndex() const noexcept;
        [[nodiscard]] winrt::hstring PauseAllLabel() const;
        [[nodiscard]] winrt::hstring PauseAllGlyph() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PauseAllVisibility() const noexcept;
        [[nodiscard]] bool IsPausedAll() const noexcept;
        [[nodiscard]] winrt::hstring RateValue() const;
        [[nodiscard]] winrt::hstring RateUnit() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility RateUnitVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility RateNormalVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility RateIdleVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility RatePausedVisibility() const noexcept;
        [[nodiscard]] winrt::hstring ConnectionsText() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ConnectionsVisibility() const noexcept;
        [[nodiscard]] winrt::hstring RateNoteText() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility RateNoteVisibility() const noexcept;
        [[nodiscard]] winrt::hstring PeakText() const;
        [[nodiscard]] winrt::hstring StorageValue() const;
        [[nodiscard]] winrt::hstring StorageUnitLine() const;
        [[nodiscard]] winrt::hstring StoredLine() const;
        [[nodiscard]] winrt::hstring InFlightLine() const;
        [[nodiscard]] Microsoft::UI::Xaml::GridLength StoredWidth() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::GridLength FlightWidth() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::GridLength FreeWidth() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility StoredVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility FlightVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility QueueVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ReadySectionVisibility() const noexcept;
        [[nodiscard]] winrt::hstring ReadyHeadline() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility EmptyVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility NoMatchesVisibility() const noexcept;
        [[nodiscard]] winrt::hstring NoMatchesLine() const;
        [[nodiscard]] winrt::HaloDesktop::DownloadRowViewModel SelectedRow() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility DetailVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility FolderVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SelectedReadyVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SelectedTransferVisibility() const noexcept;

        void Select(winrt::hstring const& id);
        // Opens a row's details, or shuts them when they are the ones open.
        void ToggleDetails(winrt::hstring const& id);
        [[nodiscard]] Microsoft::UI::Xaml::Visibility RunningVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility WaitingVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility AttentionVisibility() const noexcept;
        [[nodiscard]] winrt::hstring WaitingLine() const;
        // The status card's second line: the rate (or that all is paused) and the free space.
        [[nodiscard]] winrt::hstring StatusLine() const;
        void SetFilter(std::int32_t index);
        void Pause(winrt::hstring const& id);
        void Resume(winrt::hstring const& id);
        void Retry(winrt::hstring const& id);
        void Cancel(winrt::hstring const& id);
        void Delete(winrt::hstring const& id);
        void ChooseSourceFor(winrt::hstring const& id);
        void Play(winrt::hstring const& id);
        void OpenDownloadFolder();
        void RevealSelected();
        void OpenDetails(winrt::hstring const& id);
        void RetryFailed();
        void PauseAll();
        void ResumeAll();
        void PauseSelected();
        void ResumeSelected();
        void CancelSelected();
        void DeleteAll();
        void OpenPlayer();
        void ChooseSource();
        void BrowseLibrary();
        // Where downloads go now; empty until the download service reports it.
        [[nodiscard]] std::filesystem::path CurrentDownloadDirectory() const;
        [[nodiscard]] winrt::Windows::Foundation::IAsyncAction SetDownloadDirectoryAsync(
            std::filesystem::path directory);
        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        // The parsed release facts of one download, kept while the fields they
        // were parsed from stay the same: the parser runs a dozen expressions,
        // and every progress tick rebuilds every row.
        struct CachedFacts final
        {
            std::tuple<std::optional<std::wstring>, std::optional<std::wstring>, std::wstring, std::uint64_t, std::optional<std::uint64_t>, std::optional<std::wstring>> Key;
            ::HaloDesktop::ViewModels::DownloadsPresentation::StreamFacts Facts;
        };

        void Synchronize();
        void ApplyFilteredViews();
        void ApplySelection();
        void RebuildChart();
        void RaiseState();
        [[nodiscard]] ::HaloDesktop::ViewModels::DownloadsPresentation::StreamFacts FactsFor(
            ::HaloDesktop::Services::Downloads::DownloadRecord const& record);
        [[nodiscard]] DownloadRowViewModel* RowFor(std::wstring const& id) const;
        [[nodiscard]] ::HaloDesktop::ViewModels::DownloadsPresentation::DownloadRow const* SelectedProjection() const;

        std::shared_ptr<::HaloDesktop::Services::IDownloadService> m_downloads;
        std::shared_ptr<::HaloDesktop::Services::NavigationService> m_navigation;
        // Every download's row view model, by id, so a row keeps its object (and
        // its list container) for as long as the download exists.
        std::unordered_map<std::wstring, winrt::HaloDesktop::DownloadRowViewModel> m_rows;
        std::unordered_map<std::wstring, CachedFacts> m_facts;
        std::vector<::HaloDesktop::ViewModels::DownloadsPresentation::DownloadRow> m_transferRows;
        std::vector<::HaloDesktop::ViewModels::DownloadsPresentation::DownloadRow> m_readyRows;
        std::vector<::HaloDesktop::ViewModels::DownloadsPresentation::DownloadRow> m_shownTransfers;
        std::vector<::HaloDesktop::ViewModels::DownloadsPresentation::DownloadRow> m_shownReady;
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_runningView{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_waitingView{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_attentionView{ nullptr };
        // The row whose details are open; one at a time, none at first.
        std::wstring m_expandedId;
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_filteredReady{ nullptr };
        std::vector<double> m_chartValues;
        std::vector<::HaloDesktop::ViewModels::DownloadsPresentation::ChartBar> m_chartBars;
        bool m_chartBuilt{};
        ::HaloDesktop::ViewModels::DownloadsPresentation::StorageFigures m_storage;
        ::HaloDesktop::ViewModels::DownloadsPresentation::RateNote m_rateNote;
        double m_rate{};
        bool m_pausedAll{};
        std::size_t m_activeCount{};
        ::HaloDesktop::ViewModels::DownloadsPresentation::Filter m_filter{ ::HaloDesktop::ViewModels::DownloadsPresentation::Filter::All };
        winrt::HaloDesktop::DownloadRowViewModel m_selected{ nullptr };
        std::wstring m_selectedId;
        ::HaloDesktop::Services::DownloadChangedToken m_changedToken{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
