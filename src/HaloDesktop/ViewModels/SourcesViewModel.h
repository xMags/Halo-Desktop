#pragma once

#include "SourceDisplayItemViewModel.g.h"
#include "SourceResolutionItemViewModel.g.h"
#include "SourceQualityItemViewModel.g.h"
#include "SourcePickerRuleViewModel.g.h"
#include "SourcesViewModel.g.h"
#include "Services/AppServices.h"
#include "Services/ServiceInterfaces.h"

#include <cstdint>
#include <memory>
#include <winrt/Microsoft.UI.Xaml.Data.h>

namespace winrt::HaloDesktop::implementation
{
    // One row of the aside cards. Each carries only what its card renders, so the
    // page never has to take a formatted string apart to lay it out.
    struct SourceResolutionItemViewModel : SourceResolutionItemViewModelT<SourceResolutionItemViewModel>
    {
        SourceResolutionItemViewModel(winrt::hstring name, winrt::hstring value, bool resolved);
        [[nodiscard]] winrt::hstring Name() const;
        [[nodiscard]] winrt::hstring Value() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ResolvedVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility FailedVisibility() const noexcept;

    private:
        winrt::hstring m_name;
        winrt::hstring m_value;
        bool m_resolved{};
    };

    struct SourceQualityItemViewModel : SourceQualityItemViewModelT<SourceQualityItemViewModel>
    {
        SourceQualityItemViewModel(winrt::hstring label, std::int32_t count, std::int32_t largest);
        [[nodiscard]] winrt::hstring Label() const;
        [[nodiscard]] winrt::hstring Count() const;
        // 0..1 of the largest bucket, so the widest bar always fills its track.
        [[nodiscard]] double Share() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility Quality2160Visibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility Quality1080Visibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility QualityOtherVisibility() const noexcept;

    private:
        winrt::hstring m_label;
        std::int32_t m_count{};
        std::int32_t m_largest{};
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

    struct SourceDisplayItemViewModel : SourceDisplayItemViewModelT<SourceDisplayItemViewModel>
    {
        SourceDisplayItemViewModel(winrt::hstring groupName, winrt::hstring groupNote, winrt::hstring groupCount);
        SourceDisplayItemViewModel(winrt::HaloDesktop::StreamSource source, bool detailColumns);
        [[nodiscard]] Microsoft::UI::Xaml::Visibility HeaderVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility RowVisibility() const noexcept;
        [[nodiscard]] winrt::hstring Key() const;
        [[nodiscard]] winrt::hstring GroupName() const;
        [[nodiscard]] winrt::hstring GroupNote() const;
        [[nodiscard]] winrt::hstring GroupCount() const;
        [[nodiscard]] winrt::hstring Quality() const;
        [[nodiscard]] winrt::hstring Range() const;
        [[nodiscard]] winrt::hstring File() const;
        [[nodiscard]] winrt::hstring Codec() const;
        [[nodiscard]] winrt::hstring Audio() const;
        [[nodiscard]] winrt::hstring Languages() const;
        [[nodiscard]] winrt::hstring StatusLabel() const;
        [[nodiscard]] winrt::hstring Size() const;
        [[nodiscard]] winrt::hstring Detail() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility Quality2160Visibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility Quality1080Visibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility QualityOtherVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility InstantVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility CachingVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility UncachedVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility OnDiskVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility UnknownVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility DetailColumnVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility CompactSummaryVisibility() const noexcept;

    private:
        winrt::hstring m_groupName;
        winrt::hstring m_groupNote;
        winrt::hstring m_groupCount;
        winrt::HaloDesktop::StreamSource m_source{ nullptr };
        bool m_isHeader{};
        bool m_detailColumns{};
    };

    struct SourcesViewModel : SourcesViewModelT<SourcesViewModel>
    {
        explicit SourcesViewModel(::HaloDesktop::Services::AppServices const& services);
        ~SourcesViewModel();
        void Activate();
        void Deactivate() noexcept;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable Items() const;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable ResolutionItems() const;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable QualityItems() const;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> ItemsView() const;
        [[nodiscard]] winrt::hstring Title() const;
        [[nodiscard]] winrt::hstring Poster() const;
        [[nodiscard]] winrt::hstring EpisodeLabel() const;
        [[nodiscard]] winrt::hstring ResolveSummary() const;
        [[nodiscard]] winrt::hstring AllFilterLabel() const;
        [[nodiscard]] winrt::hstring InstantFilterLabel() const;
        [[nodiscard]] winrt::hstring Quality2160FilterLabel() const;
        [[nodiscard]] winrt::hstring Quality1080FilterLabel() const;
        [[nodiscard]] winrt::hstring BestQuality() const;
        [[nodiscard]] winrt::hstring BestRange() const;
        [[nodiscard]] winrt::hstring BestFile() const;
        [[nodiscard]] winrt::hstring BestCodec() const;
        [[nodiscard]] winrt::hstring BestAudio() const;
        [[nodiscard]] winrt::hstring BestLanguages() const;
        [[nodiscard]] winrt::hstring BestSize() const;
        [[nodiscard]] winrt::hstring BestStatusLine() const;
        [[nodiscard]] winrt::hstring BestStatusBadge() const;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable PickerRules() const;
        [[nodiscard]] winrt::hstring TeachingTipTitle() const;
        [[nodiscard]] winrt::hstring TeachingTipBody() const;
        [[nodiscard]] bool TeachingTipOpen() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ContentVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility LoadingVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ErrorVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility EmptyVisibility() const noexcept;
        void Load(winrt::Windows::Foundation::IInspectable const& parameter);
        void Retry();
        void SetFilter(std::int32_t index);
        void DismissTeachingTip();
        void OpenPlayer(winrt::hstring const& key);
        void OpenBest();
        void OpenSettings();
        void GoBack();
        [[nodiscard]] Microsoft::UI::Xaml::Visibility DetailColumnVisibility() const noexcept;
        // Called by the page with the measured width of the list column. The
        // optional columns depend on the room the list actually has, which the
        // window width cannot answer: the nav rail and the aside both take a cut.
        void SetListWidth(double width);
        [[nodiscard]] concurrency::task<::HaloDesktop::Services::DownloadStartOutcome> StartDownloadAsync(
            winrt::hstring key,
            bool replaceExisting);
        [[nodiscard]] winrt::hstring BestKey() const;
        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        [[nodiscard]] winrt::Windows::Foundation::IAsyncAction LoadAsync();
        void Rebuild();
        void RebuildAside();
        void RaiseState();
        void Raise(wchar_t const* property);
        [[nodiscard]] bool MatchesFilter(winrt::HaloDesktop::StreamSource const& source) const;

        std::shared_ptr<::HaloDesktop::Services::ISourceService> m_sources;
        std::shared_ptr<::HaloDesktop::Services::NavigationService> m_navigation;
        std::shared_ptr<::HaloDesktop::Services::SettingsSyncService> m_settings;
        std::shared_ptr<::HaloDesktop::Services::IDownloadService> m_downloads;
        std::shared_ptr<::HaloDesktop::Services::DevicePreferencesStore> m_devicePreferences;
        winrt::HaloDesktop::SourcesNavParams m_parameters{ nullptr };
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::SourceGroup> m_sourceGroups{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_items{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_resolutionItems{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_qualityItems{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_pickerRules{ nullptr };
        winrt::HaloDesktop::StreamSource m_bestSource{ nullptr };
        std::int32_t m_filterIndex{};
        bool m_detailColumns{};
        std::uint32_t m_loadVersion{};
        bool m_loading{};
        bool m_error{};
        bool m_teachingTipOpen{ true };
        ::HaloDesktop::Services::DownloadChangedToken m_downloadToken{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
