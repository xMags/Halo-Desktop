#pragma once

#include "RecentSearchViewModel.g.h"
#include "SearchViewModel.g.h"
#include "Services/AppServices.h"
#include "Services/ServiceInterfaces.h"
#include <cstdint>
#include <memory>
#include <winrt/Microsoft.UI.Xaml.Data.h>

namespace winrt::HaloDesktop::implementation
{
    struct RecentSearchViewModel : RecentSearchViewModelT<RecentSearchViewModel>
    {
        RecentSearchViewModel(winrt::hstring term, winrt::hstring age);
        [[nodiscard]] winrt::hstring Term() const;
        [[nodiscard]] winrt::hstring Age() const;
    private:
        winrt::hstring m_term;
        winrt::hstring m_age;
    };

    struct SearchViewModel : SearchViewModelT<SearchViewModel>
    {
        explicit SearchViewModel(::HaloDesktop::Services::AppServices const& services);
        [[nodiscard]] winrt::hstring Query() const;
        void Query(winrt::hstring const& value);
        [[nodiscard]] winrt::Windows::Foundation::IInspectable Results() const;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable RecentItems() const;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> ResultsView() const;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> RecentItemsView() const;
        [[nodiscard]] std::int32_t AllCount() const noexcept;
        [[nodiscard]] std::int32_t MovieCount() const noexcept;
        [[nodiscard]] std::int32_t SeriesCount() const noexcept;
        [[nodiscard]] winrt::hstring TopMatchTitle() const;
        [[nodiscard]] winrt::hstring TopMatchMeta() const;
        [[nodiscard]] winrt::hstring TopMatchSynopsis() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility TopMatchVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ResultsVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility RecentVisibility() const noexcept;
        void SetFilter(std::int32_t index);
        void Submit(winrt::hstring const& query);
        void Clear();
        void OpenDetail();
        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        void Rebuild();
        void RaiseState();
        std::shared_ptr<::HaloDesktop::Services::ICatalogService> m_catalog;
        std::shared_ptr<::HaloDesktop::Services::NavigationService> m_navigation;
        winrt::hstring m_query;
        winrt::HaloDesktop::MediaSummary m_topMatch{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_results{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_recentItems{ nullptr };
        std::int32_t m_filterIndex{};
        std::int32_t m_movieCount{};
        std::int32_t m_seriesCount{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
