#pragma once

#include "HomeViewModel.g.h"
#include "Services/AppServices.h"
#include "Services/ServiceInterfaces.h"
#include <cstdint>
#include <memory>
#include <winrt/Microsoft.UI.Xaml.Data.h>

namespace winrt::HaloDesktop::implementation
{
    struct HomeViewModel : HomeViewModelT<HomeViewModel>
    {
        explicit HomeViewModel(::HaloDesktop::Services::AppServices const& services);
        [[nodiscard]] winrt::hstring HeroTitle() const;
        [[nodiscard]] winrt::hstring HeroSynopsis() const;
        [[nodiscard]] winrt::hstring HeroRating() const;
        [[nodiscard]] winrt::hstring HeroMeta() const;
        [[nodiscard]] winrt::hstring CatalogStats() const;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable ContinueItems() const;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable Shelves() const;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> ContinueItemsView() const;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> ShelvesView() const;
        [[nodiscard]] std::int32_t FilterIndex() const noexcept;
        void SetFilter(std::int32_t index);
        void OpenDetail();
        void OpenPlayer();
        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        void RebuildShelves();
        std::shared_ptr<::HaloDesktop::Services::ICatalogService> m_catalog;
        std::shared_ptr<::HaloDesktop::Services::NavigationService> m_navigation;
        winrt::HaloDesktop::MediaSummary m_hero{ nullptr };
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::ContinueItem> m_continueItems{ nullptr };
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Shelf> m_sourceShelves{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_continueItemsView{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_shelves{ nullptr };
        std::int32_t m_filterIndex{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
