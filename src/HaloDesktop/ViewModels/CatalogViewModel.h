#pragma once

#include "CatalogViewModel.g.h"
#include "Services/AppServices.h"
#include "Services/CatalogPagingPolicy.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace HaloDesktop::Services
{
    class ICatalogService;
}

namespace winrt::HaloDesktop::implementation
{
    struct CatalogViewModel : CatalogViewModelT<CatalogViewModel>
    {
        explicit CatalogViewModel(::HaloDesktop::Services::AppServices const& services);

        [[nodiscard]] winrt::hstring Title() const;
        [[nodiscard]] winrt::hstring SourceLabel() const;
        [[nodiscard]] winrt::hstring CountLabel() const;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable Items() const;
        [[nodiscard]] auto ItemsView() const { return m_items; }
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SourceVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ContentVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility EmptyVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility MoreVisibility() const noexcept;
        [[nodiscard]] bool LoadingMore() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility LoadingMoreVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility MoreFailedVisibility() const noexcept;

        void Load(winrt::Windows::Foundation::IInspectable const& parameter);
        void RequestMore();
        void RetryMore();
        void OpenDetail(winrt::Windows::Foundation::IInspectable const& item);
        void GoBack();

        winrt::event_token PropertyChanged(
            Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        winrt::Windows::Foundation::IAsyncAction LoadMoreAsync();
        void RaiseState();
        void RaiseMore();

        std::shared_ptr<::HaloDesktop::Services::NavigationService> m_navigation;
        std::shared_ptr<::HaloDesktop::Services::ICatalogService> m_catalog;
        winrt::hstring m_title{ L"Catalog" };
        winrt::hstring m_sourceLabel;
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::MediaSummary> m_snapshotItems{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_items{ nullptr };
        // Set only for a catalog that pages.
        std::shared_ptr<::HaloDesktop::Services::CatalogPagingSource const> m_paging;
        std::optional<::HaloDesktop::Services::CatalogPager> m_pager;
        // Advanced by every Load, so a page asked for by an earlier grid is
        // never appended to a later one.
        std::uint64_t m_pageGeneration{};
        bool m_loadingMore{};
        bool m_moreFailed{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
