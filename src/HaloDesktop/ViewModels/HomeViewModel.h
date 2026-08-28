#pragma once
#include "HomeViewModel.g.h"
#include "Services/AppServices.h"
#include "Services/ServiceInterfaces.h"
#include <cstdint>
#include <memory>
#include <vector>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Xaml.h>
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
        [[nodiscard]] winrt::Windows::Foundation::IInspectable FeaturedItems() const;
        [[nodiscard]] auto FeaturedItemsView() const { return m_featured; }
        [[nodiscard]] std::int32_t FeaturedCount() const noexcept;
        [[nodiscard]] std::int32_t FeaturedIndex() const noexcept;
        void FeaturedIndex(std::int32_t value);
        [[nodiscard]] Microsoft::UI::Xaml::Visibility FeaturedVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility RefreshErrorVisibility() const noexcept;
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
        void OpenFeaturedDetail(winrt::Windows::Foundation::IInspectable const& item);
        void OpenFeaturedSources(winrt::Windows::Foundation::IInspectable const& item);
        void ToggleFeaturedLibrary(winrt::Windows::Foundation::IInspectable const& item);
        // The strip stops advancing while a pointer rests on it, and stops
        // entirely once Home is off screen: a timer ticking behind another page
        // would animate a carousel nobody is looking at.
        void PauseFeatured();
        void ResumeFeatured();
        void Deactivate();
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
        winrt::Windows::Foundation::IAsyncAction ToggleFeaturedLibraryAsync(winrt::HaloDesktop::FeaturedItem item);
        void AdoptSnapshot();
        void ApplyContinue();
        void Rebuild();
        void RebuildFeatured();
        void SynchronizeFeaturedLibraryState();
        void RestartFeaturedTimer();
        void StopFeaturedTimer();
        void AdvanceFeatured();
        [[nodiscard]] bool HasUsableContent() const noexcept;
        void RaiseState();
        void RaiseLayoutMetrics();
        void Raise(wchar_t const* name);
        std::shared_ptr<::HaloDesktop::Shell::LayoutMetricsService> m_layout;
        std::shared_ptr<::HaloDesktop::Services::ICatalogService> m_catalog;
        std::shared_ptr<::HaloDesktop::Services::LibraryService> m_library;
        std::uint64_t m_metricsToken{};
        std::shared_ptr<::HaloDesktop::Services::NavigationService> m_navigation;
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_featured{ nullptr };
        Microsoft::UI::Xaml::DispatcherTimer m_featuredTimer{ nullptr };
        winrt::event_token m_featuredTickToken{};
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Shelf> m_sourceShelves{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_continueItems{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_shelves{ nullptr };
        std::int32_t m_filterIndex{};
        std::int32_t m_featuredIndex{};
        std::uint32_t m_loadVersion{};
        std::uint64_t m_appliedVersion{};
        bool m_loading{ true };
        bool m_error{};
        bool m_refreshError{};
        bool m_active{};
        bool m_featuredPaused{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
