#pragma once
#include "HomeViewModel.g.h"
#include "Services/AppServices.h"
#include "Services/ServiceInterfaces.h"
#include <cstdint>
#include <memory>
#include <winrt/Microsoft.UI.Xaml.Data.h>
namespace HaloDesktop::Shell
{
    class LayoutMetricsService;
}

namespace winrt::HaloDesktop::implementation
{
    struct HomeViewModel : HomeViewModelT<HomeViewModel>
    {
        explicit HomeViewModel(::HaloDesktop::Services::AppServices const& services);
        ~HomeViewModel();
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
        void EnsureLoaded();
        void OpenDetail(winrt::Windows::Foundation::IInspectable const& item);
        void OpenHeroDetail();
        void OpenHeroSources();
        void OpenContinue(winrt::Windows::Foundation::IInspectable const& item);
        void OpenSearch(winrt::hstring const& query);
        void OpenCatalog(winrt::Windows::Foundation::IInspectable const& shelf);
        void OpenContinueCatalog();
        [[nodiscard]] double HeroHeight() const noexcept;
        [[nodiscard]] double HeroTitleSize() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Thickness ContentPadding() const noexcept;
        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;
    private:
        winrt::Windows::Foundation::IAsyncAction LoadAsync();
        void AdoptSnapshot();
        void ApplyContinue();
        void Rebuild();
        void RaiseState();
        void RaiseLayoutMetrics();
        void Raise(wchar_t const* name);
        std::shared_ptr<::HaloDesktop::Shell::LayoutMetricsService> m_layout;
        std::shared_ptr<::HaloDesktop::Services::ICatalogService> m_catalog;
        std::uint64_t m_metricsToken{};
        std::shared_ptr<::HaloDesktop::Services::NavigationService> m_navigation;
        winrt::HaloDesktop::MediaSummary m_hero{ nullptr };
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Shelf> m_sourceShelves{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_continueItems{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_shelves{ nullptr };
        std::int32_t m_filterIndex{};
        std::uint32_t m_loadVersion{};
        std::uint64_t m_appliedVersion{};
        bool m_loading{ true };
        bool m_error{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
