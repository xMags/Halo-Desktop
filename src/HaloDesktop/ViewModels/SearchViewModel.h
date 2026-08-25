#pragma once
#include "RecentSearchViewModel.g.h"
#include "SearchViewModel.g.h"
#include "Services/AppServices.h"
#include "Services/ServiceInterfaces.h"
#include <cstdint>
#include <memory>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Data.h>
namespace winrt::HaloDesktop::implementation
{
    struct RecentSearchViewModel : RecentSearchViewModelT<RecentSearchViewModel>
    {
        RecentSearchViewModel(winrt::hstring term, winrt::hstring age); winrt::hstring Term() const; winrt::hstring Age() const;
    private: winrt::hstring m_term; winrt::hstring m_age;
    };
    struct SearchViewModel : SearchViewModelT<SearchViewModel>
    {
        explicit SearchViewModel(::HaloDesktop::Services::AppServices const& services);
        winrt::hstring Query() const; void Query(winrt::hstring const& value);
        winrt::Windows::Foundation::IInspectable Results() const; winrt::Windows::Foundation::IInspectable RecentItems() const;
        auto ResultsView() const { return m_results; } auto RecentItemsView() const { return m_recentItems; }
        std::int32_t AllCount() const noexcept; std::int32_t MovieCount() const noexcept; std::int32_t SeriesCount() const noexcept;
        winrt::hstring TopMatchTitle() const; winrt::hstring TopMatchMeta() const; winrt::hstring TopMatchSynopsis() const; winrt::hstring TopMatchPoster() const;
        Microsoft::UI::Xaml::Visibility TopMatchVisibility() const noexcept; Microsoft::UI::Xaml::Visibility ResultsVisibility() const noexcept; Microsoft::UI::Xaml::Visibility RecentVisibility() const noexcept;
        Microsoft::UI::Xaml::Visibility LoadingVisibility() const noexcept; Microsoft::UI::Xaml::Visibility ErrorVisibility() const noexcept; Microsoft::UI::Xaml::Visibility EmptyVisibility() const noexcept;
        void SetFilter(std::int32_t index); void Submit(winrt::hstring const& query); void Clear(); void Retry(); void OpenDetail(winrt::Windows::Foundation::IInspectable const& item); void OpenTopMatch(); void OpenCatalog(winrt::Windows::Foundation::IInspectable const& shelf);
        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler); void PropertyChanged(winrt::event_token const& token) noexcept;
    private:
        winrt::Windows::Foundation::IAsyncAction SearchAsync(bool deliberate);
        void Rebuild(); void LoadRecents(); void RaiseState(); void Raise(wchar_t const* name);
        std::shared_ptr<::HaloDesktop::Services::ICatalogService> m_catalog; std::shared_ptr<::HaloDesktop::Services::NavigationService> m_navigation;
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_debounceTimer{ nullptr };
        winrt::hstring m_query; winrt::HaloDesktop::MediaSummary m_topMatch{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_results{ nullptr },m_recentItems{ nullptr };
        std::int32_t m_filterIndex{},m_movieCount{},m_seriesCount{}; std::uint32_t m_searchVersion{}; bool m_loading{},m_error{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
