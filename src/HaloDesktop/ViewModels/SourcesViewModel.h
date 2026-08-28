#pragma once

#include "SourceDisplayItemViewModel.g.h"
#include "SourceProviderItemViewModel.g.h"
#include "SourceQualityItemViewModel.g.h"
#include "SourcePickerRuleViewModel.g.h"
#include "SourceSpecItemViewModel.g.h"
#include "SourcesViewModel.g.h"
#include "Services/AppServices.h"
#include "Services/ServiceInterfaces.h"
#include "ViewModels/SourcePresentation.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
#include <winrt/Microsoft.UI.Xaml.Data.h>

namespace winrt::HaloDesktop::implementation
{
    // One labelled row of the expander's specification grid.
    struct SourceSpecItemViewModel : SourceSpecItemViewModelT<SourceSpecItemViewModel>
    {
        SourceSpecItemViewModel(winrt::hstring key, winrt::hstring value);
        [[nodiscard]] winrt::hstring Key() const;
        [[nodiscard]] winrt::hstring Value() const;

    private:
        winrt::hstring m_key;
        winrt::hstring m_value;
    };

    // One addon, in the resolving list and again in the footer's provider column.
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
        SourceQualityItemViewModel(::HaloDesktop::Sources::QualityTier tier, std::int32_t count, std::int32_t total);
        [[nodiscard]] winrt::hstring Label() const;
        [[nodiscard]] winrt::hstring Count() const;
        // 0..1 of the whole pool, so the three bars read as shares of one list.
        [[nodiscard]] double Share() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility TopTierVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility MidTierVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility LowTierVisibility() const noexcept;

    private:
        ::HaloDesktop::Sources::QualityTier m_tier{};
        std::int32_t m_count{};
        std::int32_t m_total{};
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
        enum class Kind
        {
            GroupHeader,
            Row,
            ShowMore,
        };

        // Group header.
        SourceDisplayItemViewModel(winrt::hstring name, winrt::hstring note, winrt::hstring count);
        // Dashed reveal row for the collapsed cold group.
        explicit SourceDisplayItemViewModel(winrt::hstring showMoreLabel);
        // Source row.
        SourceDisplayItemViewModel(
            ::HaloDesktop::Sources::SourceEntry entry,
            winrt::hstring statusLabel,
            winrt::hstring soundAndSize,
            winrt::hstring languageLine,
            winrt::hstring warning,
            winrt::hstring reason,
            std::vector<::HaloDesktop::Sources::SpecRow> specs);

        [[nodiscard]] Microsoft::UI::Xaml::Visibility HeaderVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility RowVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ShowMoreVisibility() const noexcept;

        [[nodiscard]] winrt::hstring GroupName() const;
        [[nodiscard]] winrt::hstring GroupNote() const;
        [[nodiscard]] winrt::hstring GroupCount() const;
        [[nodiscard]] winrt::hstring ShowMoreLabel() const;

        [[nodiscard]] winrt::hstring Key() const;
        [[nodiscard]] winrt::hstring QualityHead() const;
        [[nodiscard]] winrt::hstring QualitySub() const;
        [[nodiscard]] winrt::hstring StatusLabel() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility InstantVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility OnDiskVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility CachingVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ColdVisibility() const noexcept;
        [[nodiscard]] winrt::hstring SoundAndSize() const;
        [[nodiscard]] winrt::hstring LanguageLine() const;
        [[nodiscard]] winrt::hstring Warning() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility WarningVisibility() const noexcept;
        [[nodiscard]] winrt::hstring FileName() const;
        [[nodiscard]] winrt::hstring Reason() const;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable Specs() const;

        [[nodiscard]] Microsoft::UI::Xaml::Visibility ExpandedVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ExpandGlyphVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility CollapseGlyphVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SelectionVisibility() const noexcept;

        [[nodiscard]] bool IsRow() const noexcept;
        [[nodiscard]] ::HaloDesktop::Sources::SourceEntry const& Entry() const noexcept;
        void SetExpanded(bool value);
        void SetSelected(bool value);

        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        void Raise(wchar_t const* property);

        Kind m_kind{ Kind::Row };
        winrt::hstring m_groupName;
        winrt::hstring m_groupNote;
        winrt::hstring m_groupCount;
        winrt::hstring m_showMoreLabel;
        ::HaloDesktop::Sources::SourceEntry m_entry;
        winrt::hstring m_statusLabel;
        winrt::hstring m_soundAndSize;
        winrt::hstring m_languageLine;
        winrt::hstring m_warning;
        winrt::hstring m_reason;
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_specs{ nullptr };
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

        [[nodiscard]] double SheetWidth() const noexcept;
        [[nodiscard]] winrt::hstring Kicker() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility KickerVisibility() const noexcept;
        [[nodiscard]] winrt::hstring Heading() const;
        [[nodiscard]] winrt::hstring CountLine() const;

        [[nodiscard]] winrt::hstring AllFilterCount() const;
        [[nodiscard]] winrt::hstring PlaysNowFilterCount() const;
        [[nodiscard]] winrt::hstring UltraHdFilterCount() const;
        [[nodiscard]] winrt::hstring FullHdFilterCount() const;
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
        [[nodiscard]] winrt::hstring PickQualityHead() const;
        [[nodiscard]] winrt::hstring PickQualitySub() const;
        [[nodiscard]] winrt::hstring PickStatusLabel() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PickInstantVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PickOnDiskVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PickCachingVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PickColdVisibility() const noexcept;
        [[nodiscard]] winrt::hstring PickWhy() const;
        [[nodiscard]] winrt::hstring PickLine() const;
        [[nodiscard]] winrt::hstring PickFileName() const;
        [[nodiscard]] winrt::hstring PickWatchNote() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PickWatchNoteVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PickSelectionVisibility() const noexcept;

        [[nodiscard]] Microsoft::UI::Xaml::Visibility InfoVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility InfoExpandGlyphVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility InfoCollapseGlyphVisibility() const noexcept;

        // Index into Items of the selected row, or -1 while the pick block holds
        // the selection. The page uses it to scroll the selection into view.
        [[nodiscard]] std::int32_t SelectedIndex() const noexcept;

        void Load(winrt::Windows::Foundation::IInspectable const& parameter);
        void Retry();
        void SetFilter(std::int32_t index);
        void SetSort(std::int32_t index);
        void ToggleInfo();
        void ToggleExpanded(winrt::hstring const& key);
        void SelectKey(winrt::hstring const& key);
        void SelectPick();
        void MoveSelection(std::int32_t delta);
        void ExpandSelected();
        void CollapseSelected();
        void RevealCold();
        void PlaySelected();
        void OpenPlayer(winrt::hstring const& key);
        void OpenSettings();
        void Close();
        [[nodiscard]] winrt::hstring FileNameFor(winrt::hstring const& key) const;

        [[nodiscard]] concurrency::task<::HaloDesktop::Services::DownloadStartOutcome> StartDownloadAsync(
            winrt::hstring key,
            bool replaceExisting);
        // Key of the selected row, empty while the pick block holds the selection.
        [[nodiscard]] winrt::hstring SelectedKey() const;
        [[nodiscard]] winrt::hstring PickKey() const;

        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        [[nodiscard]] winrt::Windows::Foundation::IAsyncAction LoadAsync();
        void AdoptResolve();
        void Rebuild();
        void RebuildFooter();
        void ApplySelection();
        void RaiseState();
        void Raise(wchar_t const* property);
        void ApplyLayoutMetrics();
        [[nodiscard]] std::vector<::HaloDesktop::Sources::SourceEntry> Filtered(std::int32_t filterIndex) const;
        [[nodiscard]] bool Matches(::HaloDesktop::Sources::SourceEntry const& entry, std::int32_t filterIndex) const noexcept;
        [[nodiscard]] winrt::HaloDesktop::SourceDisplayItemViewModel RowFor(
            ::HaloDesktop::Sources::SourceEntry const& entry,
            ::HaloDesktop::Sources::SourceEntry const* pick) const;
        [[nodiscard]] std::int32_t IndexOfKey(winrt::hstring const& key) const;
        [[nodiscard]] bool HasPick() const noexcept;

        std::shared_ptr<::HaloDesktop::Services::ISourceService> m_sources;
        std::shared_ptr<::HaloDesktop::Services::IMetadataService> m_metadata;
        std::shared_ptr<::HaloDesktop::Services::NavigationService> m_navigation;
        std::shared_ptr<::HaloDesktop::Services::SettingsSyncService> m_settings;
        std::shared_ptr<::HaloDesktop::Services::IDownloadService> m_downloads;
        std::shared_ptr<::HaloDesktop::Services::DevicePreferencesStore> m_devicePreferences;
        std::shared_ptr<::HaloDesktop::Services::WatchStateService> m_watch;
        std::shared_ptr<::HaloDesktop::Shell::LayoutMetricsService> m_layout;

        winrt::HaloDesktop::SourcesNavParams m_parameters{ nullptr };
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::SourceGroup> m_sourceGroups{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_items{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_providerItems{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_qualityItems{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_pickerRules{ nullptr };

        // The whole resolve, decorated once, in the service's ranking order. Every
        // filter and sort is a view over this rather than a fresh derivation.
        std::vector<::HaloDesktop::Sources::SourceEntry> m_pool;
        ::HaloDesktop::Sources::DeviceContext m_device;
        std::uint64_t m_smallestBytes{};
        std::int32_t m_answeredProviders{};
        std::int32_t m_failedProviders{};
        winrt::hstring m_firstFailure;

        std::int32_t m_filterIndex{};
        std::int32_t m_sortIndex{};
        std::int32_t m_selectedIndex{ -1 };
        winrt::hstring m_expandedKey;
        bool m_coldRevealed{};
        bool m_infoOpen{};
        std::uint32_t m_loadVersion{};
        bool m_loading{ true };
        bool m_error{};
        double m_sheetWidth{ 700.0 };
        std::uint64_t m_layoutToken{};
        ::HaloDesktop::Services::DownloadChangedToken m_downloadToken{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
