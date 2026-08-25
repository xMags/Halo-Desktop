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
        [[nodiscard]] winrt::hstring HeroBackground() const;
        [[nodiscard]] winrt::hstring HeroActionLabel() const;
        [[nodiscard]] winrt::hstring ContinueCountLabel() const;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable ContinueItems() const;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable Shelves() const;
        [[nodiscard]] auto ContinueItemsView() const { return m_continueItems; }
        [[nodiscard]] auto ShelvesView() const { return m_shelves; }
        [[nodiscard]] std::int32_t FilterIndex() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ContentVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility LoadingVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ErrorVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility EmptyVisibility() const noexcept;
        void SetFilter(std::int32_t index);
        void Retry();
        void OpenDetail(winrt::Windows::Foundation::IInspectable const& item);
        void OpenHeroDetail();
        void OpenHeroSources();
        void OpenContinue(winrt::Windows::Foundation::IInspectable const& item);
        void OpenSearch(winrt::hstring const& query);
        void OpenCatalog(winrt::Windows::Foundation::IInspectable const& shelf);
        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;
    private:
        winrt::Windows::Foundation::IAsyncAction LoadAsync();
        void Rebuild();
        void RaiseState();
        void Raise(wchar_t const* name);
        std::shared_ptr<::HaloDesktop::Services::ICatalogService> m_catalog;
        std::shared_ptr<::HaloDesktop::Services::NavigationService> m_navigation;
        winrt::HaloDesktop::MediaSummary m_hero{ nullptr };
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Shelf> m_sourceShelves{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_continueItems{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_shelves{ nullptr };
        std::int32_t m_filterIndex{};
        std::uint32_t m_loadVersion{};
        bool m_loading{ true };
        bool m_error{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
