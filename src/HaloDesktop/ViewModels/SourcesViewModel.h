#pragma once

#include "SourceDisplayItemViewModel.g.h"
#include "SourcesViewModel.g.h"
#include "Services/AppServices.h"
#include "Services/ServiceInterfaces.h"

#include <cstdint>
#include <memory>
#include <winrt/Microsoft.UI.Xaml.Data.h>

namespace winrt::HaloDesktop::implementation
{
    struct SourceDisplayItemViewModel : SourceDisplayItemViewModelT<SourceDisplayItemViewModel>
    {
        SourceDisplayItemViewModel(winrt::hstring groupName, winrt::hstring groupNote, winrt::hstring groupCount);
        explicit SourceDisplayItemViewModel(winrt::HaloDesktop::StreamSource source);
        [[nodiscard]] Microsoft::UI::Xaml::Visibility HeaderVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility RowVisibility() const noexcept;
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
        [[nodiscard]] Microsoft::UI::Xaml::Visibility Quality2160Visibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility Quality1080Visibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility QualityOtherVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility InstantVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility CachingVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility UncachedVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility OnDiskVisibility() const noexcept;

    private:
        winrt::hstring m_groupName;
        winrt::hstring m_groupNote;
        winrt::hstring m_groupCount;
        winrt::HaloDesktop::StreamSource m_source{ nullptr };
        bool m_isHeader{};
    };

    struct SourcesViewModel : SourcesViewModelT<SourcesViewModel>
    {
        explicit SourcesViewModel(::HaloDesktop::Services::AppServices const& services);
        [[nodiscard]] winrt::Windows::Foundation::IInspectable Items() const;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> ItemsView() const;
        [[nodiscard]] winrt::hstring BestQuality() const;
        [[nodiscard]] winrt::hstring BestRange() const;
        [[nodiscard]] winrt::hstring BestFile() const;
        [[nodiscard]] winrt::hstring BestCodec() const;
        [[nodiscard]] winrt::hstring BestAudio() const;
        [[nodiscard]] winrt::hstring BestLanguages() const;
        [[nodiscard]] winrt::hstring BestSize() const;
        [[nodiscard]] winrt::hstring TeachingTipTitle() const;
        [[nodiscard]] winrt::hstring TeachingTipBody() const;
        [[nodiscard]] bool TeachingTipOpen() const noexcept;
        void SetFilter(std::int32_t index);
        void DismissTeachingTip();
        void OpenPlayer();
        void OpenSettings();
        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        void Rebuild();
        [[nodiscard]] bool MatchesFilter(winrt::HaloDesktop::StreamSource const& source) const;
        std::shared_ptr<::HaloDesktop::Services::ISourceService> m_sources;
        std::shared_ptr<::HaloDesktop::Services::NavigationService> m_navigation;
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::SourceGroup> m_sourceGroups{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_items{ nullptr };
        winrt::HaloDesktop::StreamSource m_bestSource{ nullptr };
        std::int32_t m_filterIndex{};
        bool m_teachingTipOpen{ true };
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
