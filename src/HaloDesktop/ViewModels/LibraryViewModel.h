#pragma once
#include "LibraryViewModel.g.h"
#include "Services/AppServices.h"
#include "Services/ServiceInterfaces.h"
#include <cstdint>
#include <memory>
#include <vector>
#include <winrt/Microsoft.UI.Xaml.Data.h>
namespace winrt::HaloDesktop::implementation
{
    struct LibraryViewModel : LibraryViewModelT<LibraryViewModel>
    {
        explicit LibraryViewModel(::HaloDesktop::Services::AppServices const& services);
        [[nodiscard]] winrt::Windows::Foundation::IInspectable Items() const;
        [[nodiscard]] auto ItemsView() const { return m_items; }
        [[nodiscard]] std::int32_t FilterIndex() const noexcept;
        [[nodiscard]] std::int32_t SortIndex() const noexcept;
        [[nodiscard]] winrt::hstring AllLabel() const;
        [[nodiscard]] winrt::hstring MoviesLabel() const;
        [[nodiscard]] winrt::hstring SeriesLabel() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ContentVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility LoadingVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ErrorVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility EmptyVisibility() const noexcept;
        void SetFilter(std::int32_t index);
        void SetSort(std::int32_t index);
        void Retry();
        void OpenDetail(winrt::Windows::Foundation::IInspectable const& item);
        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;
    private:
        winrt::Windows::Foundation::IAsyncAction LoadAsync();
        void Rebuild(); void RaiseState(); void Raise(wchar_t const* name);
        std::shared_ptr<::HaloDesktop::Services::ICatalogService> m_catalog;
        std::shared_ptr<::HaloDesktop::Services::NavigationService> m_navigation;
        std::vector<winrt::HaloDesktop::MediaSummary> m_sourceItems;
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_items{ nullptr };
        std::int32_t m_filterIndex{}; std::int32_t m_sortIndex{}; std::uint32_t m_loadVersion{};
        bool m_loading{ true }; bool m_error{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
