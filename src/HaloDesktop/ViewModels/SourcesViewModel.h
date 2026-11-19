#pragma once

#include "SourceDetailsViewModel.g.h"
#include "SourceDisplayItemViewModel.g.h"
#include "SourceFactLineViewModel.g.h"
#include "SourceFactViewModel.g.h"
#include "SourcePickerRuleViewModel.g.h"
#include "SourceProviderItemViewModel.g.h"
#include "SourceQualityItemViewModel.g.h"
#include "SourceSaveViewModel.g.h"
#include "SourcesViewModel.g.h"
#include "Services/AppServices.h"
#include "Services/MagnetService.h"
#include "Services/MagnetSourcePolicy.h"
#include "Services/ServiceInterfaces.h"
#include "ViewModels/SourcePresentation.h"
#include "ViewModels/SourcesModel.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <winrt/Microsoft.UI.Xaml.Data.h>

namespace winrt::HaloDesktop::implementation
{
    struct SourceSaveViewModel : SourceSaveViewModelT<SourceSaveViewModel>
    {
        SourceSaveViewModel(winrt::hstring key, ::HaloDesktop::Sources::SaveState state);
        [[nodiscard]] winrt::hstring Key() const;
        [[nodiscard]] winrt::hstring Label() const;
        [[nodiscard]] bool IsReady() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ButtonVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ArrowVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility RingVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility CheckVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ReadyLabelVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SavingLabelVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SavedLabelVisibility() const noexcept;
        [[nodiscard]] ::HaloDesktop::Sources::SaveState const& State() const noexcept;
        // Raises only what changed, so a progress tick redraws one label.
        void SetState(::HaloDesktop::Sources::SaveState state);
        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        winrt::hstring m_key;
        ::HaloDesktop::Sources::SaveState m_state;
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };

    struct SourceFactViewModel : SourceFactViewModelT<SourceFactViewModel>
    {
        // A plain fact.
        SourceFactViewModel(winrt::hstring label, winrt::hstring value);
        // The From cell.
        SourceFactViewModel(winrt::hstring provider, winrt::hstring cacheLabel, bool cacheGood);
        [[nodiscard]] winrt::hstring Label() const;
        [[nodiscard]] winrt::hstring Value() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ValueVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ProviderVisibility() const noexcept;
        [[nodiscard]] winrt::hstring Provider() const;
        [[nodiscard]] winrt::hstring CacheLabel() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility CacheGoodVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility CacheNeutralVisibility() const noexcept;

    private:
        winrt::hstring m_label;
        winrt::hstring m_value;
        bool m_from{};
        winrt::hstring m_provider;
        winrt::hstring m_cacheLabel;
        bool m_cacheGood{};
    };

    struct SourceFactLineViewModel : SourceFactLineViewModelT<SourceFactLineViewModel>
    {
        SourceFactLineViewModel(winrt::HaloDesktop::SourceFactViewModel left, winrt::HaloDesktop::SourceFactViewModel right);
        [[nodiscard]] winrt::HaloDesktop::SourceFactViewModel Left() const;
        [[nodiscard]] winrt::HaloDesktop::SourceFactViewModel Right() const;
        [[nodiscard]] std::int32_t LeftSpan() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility RightVisibility() const noexcept;

    private:
        winrt::HaloDesktop::SourceFactViewModel m_left{ nullptr };
        winrt::HaloDesktop::SourceFactViewModel m_right{ nullptr };
    };

    struct SourceDetailsViewModel : SourceDetailsViewModelT<SourceDetailsViewModel>
    {
        SourceDetailsViewModel(
            winrt::hstring key,
            winrt::hstring fileName,
            winrt::hstring warning,
            ::HaloDesktop::Sources::SourceDetails const& details,
            winrt::HaloDesktop::SourceSaveViewModel save);
        [[nodiscard]] winrt::hstring Key() const;
        [[nodiscard]] winrt::hstring FileName() const;
        [[nodiscard]] winrt::hstring Warning() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility WarningVisibility() const noexcept;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable FactLines() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility BandwidthVisibility() const noexcept;
        [[nodiscard]] winrt::hstring BandwidthLabel() const;
        [[nodiscard]] winrt::hstring BandwidthNote() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility BandwidthNoteVisibility() const noexcept;
        [[nodiscard]] double MeterFraction() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility MeterVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility MeterNormalVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility MeterOverVisibility() const noexcept;
        [[nodiscard]] winrt::HaloDesktop::SourceSaveViewModel Save() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SaveVisibility() const noexcept;
        [[nodiscard]] winrt::hstring CopyLabel() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility CopyGlyphVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility CopiedGlyphVisibility() const noexcept;
        void SetCopied(bool copied);
        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        winrt::hstring m_key;
        winrt::hstring m_fileName;
        winrt::hstring m_warning;
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_factLines{ nullptr };
        std::optional<::HaloDesktop::Sources::BandwidthDetail> m_bandwidth;
        winrt::HaloDesktop::SourceSaveViewModel m_save{ nullptr };
        bool m_copied{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };

    // One addon in the footer's provider column.
    struct SourceProviderItemViewModel : SourceProviderItemViewModelT<SourceProviderItemViewModel>
    {
        SourceProviderItemViewModel(winrt::hstring name, winrt::hstring value, bool answered);
        [[nodiscard]] winrt::hstring Name() const;
        [[nodiscard]] winrt::hstring Value() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility AnsweredVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility FailedVisibility() const noexcept;

    private:
        winrt::hstring m_name;
        winrt::hstring m_value;
        bool m_answered{};
    };

    struct SourceQualityItemViewModel : SourceQualityItemViewModelT<SourceQualityItemViewModel>
    {
        SourceQualityItemViewModel(::HaloDesktop::Sources::QualityTier tier, std::size_t count, double share);
        [[nodiscard]] winrt::hstring Label() const;
        [[nodiscard]] winrt::hstring Count() const;
        // 0..1 of the whole pool, so the three bars read as shares of one list.
        [[nodiscard]] double Share() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility TopTierVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility MidTierVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility LowTierVisibility() const noexcept;

    private:
        ::HaloDesktop::Sources::QualityTier m_tier{};
        std::size_t m_count{};
        double m_share{};
    };

    struct SourcePickerRuleViewModel : SourcePickerRuleViewModelT<SourcePickerRuleViewModel>
    {
        SourcePickerRuleViewModel(winrt::hstring name, winrt::hstring value);
        [[nodiscard]] winrt::hstring Name() const;
        [[nodiscard]] winrt::hstring Value() const;

    private:
        winrt::hstring m_name;
        winrt::hstring m_value;
    };

    // One entry of the sheet's single list. The list is flat rather than nested so
    // arrow-key selection has one index space to walk, so the three shapes it can
    // take share one item type and switch on visibility.
    struct SourceDisplayItemViewModel : SourceDisplayItemViewModelT<SourceDisplayItemViewModel>
    {
        SourceDisplayItemViewModel(
            ::HaloDesktop::Sources::ListItem const& item,
            winrt::HaloDesktop::SourceSaveViewModel save,
            winrt::HaloDesktop::SourceDetailsViewModel details);

        [[nodiscard]] Microsoft::UI::Xaml::Visibility HeaderVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility RowVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ShowMoreVisibility() const noexcept;

        [[nodiscard]] winrt::hstring GroupName() const;
        [[nodiscard]] winrt::hstring GroupCount() const;
        [[nodiscard]] winrt::hstring ShowMoreLabel() const;

        [[nodiscard]] winrt::hstring Key() const;
        [[nodiscard]] winrt::hstring MedalPlate() const;
        [[nodiscard]] winrt::hstring MedalTag() const;
        [[nodiscard]] winrt::HaloDesktop::QualityMedalFinish MedalFinish() const;
        [[nodiscard]] winrt::hstring StatusLabel() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility InstantVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility CachingVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ColdVisibility() const noexcept;
        [[nodiscard]] winrt::hstring MetaLine() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility MetaVisibility() const noexcept;
        [[nodiscard]] winrt::hstring PlayName() const;
        [[nodiscard]] winrt::HaloDesktop::SourceSaveViewModel Save() const;
        [[nodiscard]] winrt::HaloDesktop::SourceDetailsViewModel Details() const;

        [[nodiscard]] Microsoft::UI::Xaml::Visibility ExpandedVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ExpandGlyphVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility CollapseGlyphVisibility() const noexcept;
        [[nodiscard]] winrt::hstring ChevronName() const;
        [[nodiscard]] winrt::hstring ChevronTip() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SelectionVisibility() const noexcept;

        [[nodiscard]] bool IsRow() const noexcept;
        void SetExpanded(bool value);
        void SetSelected(bool value);

        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        void Raise(wchar_t const* property);

        ::HaloDesktop::Sources::ListItem::Kind m_kind{ ::HaloDesktop::Sources::ListItem::Kind::Row };
        winrt::hstring m_groupName;
        winrt::hstring m_groupCount;
        winrt::hstring m_showMoreLabel;
        winrt::hstring m_key;
        ::HaloDesktop::ViewModels::Medal m_medal;
        ::HaloDesktop::Sources::SourceStatus m_status{ ::HaloDesktop::Sources::SourceStatus::Unknown };
        winrt::hstring m_statusLabel;
        winrt::hstring m_meta;
        winrt::hstring m_playName;
        winrt::HaloDesktop::SourceSaveViewModel m_save{ nullptr };
        winrt::HaloDesktop::SourceDetailsViewModel m_details{ nullptr };
        bool m_expanded{};
        bool m_selected{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };

    struct SourcesViewModel : SourcesViewModelT<SourcesViewModel>
    {
        explicit SourcesViewModel(::HaloDesktop::Services::AppServices const& services);
        ~SourcesViewModel();
        void Activate();
        void Deactivate() noexcept;

        [[nodiscard]] winrt::Windows::Foundation::IInspectable Items() const;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable ProviderItems() const;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable QualityItems() const;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable PickerRules() const;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> ItemsView() const;

        [[nodiscard]] winrt::hstring Kicker() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility KickerVisibility() const noexcept;
        [[nodiscard]] winrt::hstring Heading() const;
        [[nodiscard]] winrt::hstring CountLine() const;

        // The magnet pasted into Search, when one of its files is this video
        // (MagnetService::FileFor): first, outside the ranking, never the pick.
        [[nodiscard]] Microsoft::UI::Xaml::Visibility MagnetVisibility() const noexcept;
        [[nodiscard]] winrt::hstring MagnetName() const;
        [[nodiscard]] winrt::hstring MagnetFileName() const;
        [[nodiscard]] winrt::hstring MagnetSize() const;
        void PlayMagnet();

        [[nodiscard]] Microsoft::UI::Xaml::Visibility OfflineVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility OfflineMedalVisibility() const noexcept;
        [[nodiscard]] winrt::hstring OfflinePlate() const;
        [[nodiscard]] winrt::hstring OfflineTag() const;
        [[nodiscard]] winrt::HaloDesktop::QualityMedalFinish OfflineFinish() const;
        [[nodiscard]] winrt::hstring OfflineMeta() const;

        [[nodiscard]] winrt::hstring AllFilterCount() const;
        [[nodiscard]] winrt::hstring UltraHdFilterCount() const;
        [[nodiscard]] winrt::hstring FullHdFilterCount() const;
        [[nodiscard]] winrt::hstring HdFilterCount() const;
        [[nodiscard]] winrt::hstring SortLabel() const;
        [[nodiscard]] std::int32_t FilterIndex() const noexcept;
        [[nodiscard]] std::int32_t SortIndex() const noexcept;

        [[nodiscard]] Microsoft::UI::Xaml::Visibility FilterRowVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ListVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ResolvingVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility EmptyVisibility() const noexcept;
        [[nodiscard]] winrt::hstring EmptyTitle() const;
        [[nodiscard]] winrt::hstring EmptyBody() const;

        [[nodiscard]] Microsoft::UI::Xaml::Visibility BannerVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility BannerCautionVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility BannerInfoVisibility() const noexcept;
        [[nodiscard]] winrt::hstring BannerTitle() const;
        [[nodiscard]] winrt::hstring BannerBody() const;
        [[nodiscard]] winrt::hstring BannerAction() const;

        [[nodiscard]] Microsoft::UI::Xaml::Visibility PickVisibility() const noexcept;
        [[nodiscard]] winrt::hstring PickPlate() const;
        [[nodiscard]] winrt::hstring PickTag() const;
        [[nodiscard]] winrt::HaloDesktop::QualityMedalFinish PickFinish() const;
        [[nodiscard]] winrt::hstring PickStatusLabel() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PickInstantVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PickCachingVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PickColdVisibility() const noexcept;
        [[nodiscard]] winrt::hstring PickMetaLine() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PickMetaVisibility() const noexcept;
        [[nodiscard]] winrt::hstring PickFileName() const;
        [[nodiscard]] winrt::HaloDesktop::SourceSaveViewModel PickSave() const;
        [[nodiscard]] winrt::HaloDesktop::SourceDetailsViewModel PickDetails() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PickSelectionVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PickExpandedVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PickExpandGlyphVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PickCollapseGlyphVisibility() const noexcept;
        [[nodiscard]] winrt::hstring PickChevronName() const;
        [[nodiscard]] winrt::hstring PickChevronTip() const;

        [[nodiscard]] Microsoft::UI::Xaml::Visibility InfoVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility InfoOpenGlyphVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility InfoClosedGlyphVisibility() const noexcept;

        [[nodiscard]] std::int32_t SelectedIndex() const noexcept;

        void Load(winrt::Windows::Foundation::IInspectable const& parameter);
        void Retry();
        void SetFilter(std::int32_t index);
        void SetSort(std::int32_t index);
        void ToggleInfo();
        void ToggleExpanded(winrt::hstring const& key);
        void TogglePickExpanded();
        void SelectKey(winrt::hstring const& key);
        void SelectPick();
        void MoveSelection(std::int32_t delta);
        void ExpandSelected();
        void CollapseSelected();
        void RevealCold();
        void PlaySelected();
        void OpenPlayer(winrt::hstring const& key);
        bool PlayOffline();
        void OpenSettings();
        void Close();
        [[nodiscard]] winrt::hstring FileNameFor(winrt::hstring const& key) const;
        void MarkCopied(winrt::hstring const& key);

        // Marks a source as starting to save (its Save reads "Saving") until the
        // start answers; empty clears it.
        void SetStartingKey(winrt::hstring const& key);
        [[nodiscard]] concurrency::task<::HaloDesktop::Services::DownloadStartResult> StartDownloadAsync(
            winrt::hstring key,
            bool replaceExisting);
        [[nodiscard]] winrt::hstring PickKey() const;

        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        // The first resolve adopts the title page's request for the same video; Retry asks again.
        [[nodiscard]] winrt::Windows::Foundation::IAsyncAction LoadAsync(::HaloDesktop::Services::StreamReuse reuse);
        void AdoptResolve();
        void ReadDownloads();
        void Rebuild();
        void RebuildFooter();
        void RefreshSaveStates();
        void ApplySelection();
        void ApplyExpansion();
        void ResetView();
        void RaiseState();
        void RaisePick();
        void RaiseOffline();
        // Asks MagnetService which magnet file, if any, is this video.
        void ReadMagnet();
        void RaiseMagnet();
        void Raise(wchar_t const* property);
        [[nodiscard]] ::HaloDesktop::Sources::SaveState SaveStateOf(winrt::hstring const& key) const;
        [[nodiscard]] winrt::HaloDesktop::SourceSaveViewModel SaveFor(winrt::hstring const& key);
        [[nodiscard]] winrt::HaloDesktop::SourceDetailsViewModel DetailsFor(
            ::HaloDesktop::Sources::SourceEntry const& entry,
            winrt::hstring const& warning,
            ::HaloDesktop::Sources::SourceDetails const& details,
            bool withSave);
        [[nodiscard]] SourceDisplayItemViewModel* RowAt(std::uint32_t index) const;
        [[nodiscard]] std::int32_t IndexOfKey(winrt::hstring const& key) const;
        [[nodiscard]] bool HasPick() const noexcept;
        [[nodiscard]] winrt::hstring SelectedKey() const;
        [[nodiscard]] bool PoolEmpty() const noexcept;
        [[nodiscard]] bool FilteredEmpty() const noexcept;

        std::shared_ptr<::HaloDesktop::Services::ISourceService> m_sources;
        std::shared_ptr<::HaloDesktop::Services::IMetadataService> m_metadata;
        std::shared_ptr<::HaloDesktop::Services::NavigationService> m_navigation;
        std::shared_ptr<::HaloDesktop::Services::SettingsSyncService> m_settings;
        std::shared_ptr<::HaloDesktop::Services::IDownloadService> m_downloads;
        std::shared_ptr<::HaloDesktop::Services::DevicePreferencesStore> m_devicePreferences;
        std::shared_ptr<::HaloDesktop::Services::WatchStateService> m_watch;
        std::shared_ptr<::HaloDesktop::Services::MagnetService> m_magnets;
        std::optional<::HaloDesktop::Api::Dto::MagnetFile> m_magnetFile;
        std::optional<::HaloDesktop::Services::MagnetSourceRow> m_magnetRow;

        winrt::HaloDesktop::SourcesNavParams m_parameters{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_items{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_providerItems{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_qualityItems{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_pickerRules{ nullptr };

        // The whole resolve, ranked once per load. Every filter and sort is a
        // listing built over it rather than a fresh derivation.
        ::HaloDesktop::Sources::Resolve m_resolve;
        ::HaloDesktop::Sources::DeviceContext m_device;
        ::HaloDesktop::Sources::Listing m_listing;
        ::HaloDesktop::Sources::FilterCounts m_counts;
        // This video's downloads as last read: the finished one (the saved copy
        // card) and the one that decides save states.
        ::HaloDesktop::Sources::VideoDownloads m_videoDownloads;
        std::optional<::HaloDesktop::Sources::OfflineCopy> m_offline;
        // Rebuilt only when the finished download itself changes, since parsing
        // its release name is regex work and progress ticks are frequent.
        std::wstring m_offlineSignature;
        // One per key for the life of a listing, so a download moving updates the
        // label in place instead of rebuilding the list.
        std::unordered_map<std::wstring, winrt::HaloDesktop::SourceSaveViewModel> m_saves;
        winrt::HaloDesktop::SourceSaveViewModel m_pickSave{ nullptr };
        winrt::HaloDesktop::SourceDetailsViewModel m_pickDetails{ nullptr };
        winrt::hstring m_startingKey;

        std::int32_t m_filterIndex{};
        std::int32_t m_sortIndex{};
        std::int32_t m_selectedIndex{ -1 };
        winrt::hstring m_expandedKey;
        winrt::hstring m_copiedKey;
        bool m_pickExpanded{};
        bool m_coldRevealed{};
        bool m_infoOpen{};
        std::uint32_t m_loadVersion{};
        bool m_loading{ true };
        bool m_error{};
        ::HaloDesktop::Services::DownloadChangedToken m_downloadToken{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
