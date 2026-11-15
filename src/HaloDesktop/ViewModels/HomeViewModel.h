#pragma once
#include "HomeViewModel.g.h"
#include "Services/AppServices.h"
#include "Services/ServiceInterfaces.h"
#include <cstdint>
#include <memory>
#include <string>
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
        [[nodiscard]] winrt::HaloDesktop::FeaturedItem CurrentFeatured() const;
        // False once the viewer has picked a title: the carousel then stays put.
        [[nodiscard]] bool FeaturedAutoAdvance() const noexcept;
        // A manual pick, which stops the carousel on that title.
        void SelectFeatured(std::int32_t index);
        // The dwell on the current title ran out. The view's pip fill is the
        // clock, so this is only ever called when that fill completes.
        void AdvanceFeatured();
        void StepFeatured(std::int32_t direction);
        [[nodiscard]] Microsoft::UI::Xaml::Visibility FeaturedVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility RefreshErrorVisibility() const noexcept;
        [[nodiscard]] winrt::hstring ContinueCountLabel() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ContinueVisibility() const noexcept;
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
        // Home is off screen. The view stops the carousel's clock itself.
        void Deactivate();
        void OpenContinue(winrt::Windows::Foundation::IInspectable const& item);
        void OpenSearch(winrt::hstring const& query);
        // A shelf's "See all": its grid, the history or the library.
        void OpenCatalog(winrt::Windows::Foundation::IInspectable const& shelf);
        void OpenContinueCatalog();
        [[nodiscard]] double HeroHeight() const noexcept;
        [[nodiscard]] double HeroTitleSize() const noexcept;
        [[nodiscard]] double HeroLogoMaxHeight() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Thickness ContentPadding() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Thickness HeaderPadding() const noexcept;
        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;
    private:
        // The layout-step gutter, shared by ContentPadding and HeaderPadding so
        // the title and the content under it can never drift apart.
        [[nodiscard]] double Gutter() const noexcept;
        winrt::Windows::Foundation::IAsyncAction LoadAsync();
        winrt::Windows::Foundation::IAsyncAction ToggleFeaturedLibraryAsync(winrt::HaloDesktop::FeaturedItem item);
        void AdoptSnapshot();
        void AdoptUserStateChange();
        void ReloadForAccount();
        void ApplyContinue();
        // The continue row changed on its own (a next-episode lookup landed).
        // The recently watched shelf leaves out what that row offers, so it is
        // rebuilt too when its titles would change.
        void ApplyContinueChange();
        void Rebuild();
        void RebuildShelves();
        void RebuildFeatured();
        void SynchronizeFeaturedLibraryState();
        void ShowFeatured(std::int32_t index, bool autoAdvance);
        [[nodiscard]] std::vector<std::wstring> RecentlyWatchedKeys() const;
        // What the recently watched shelf shows of each title, so a rebuild is
        // skipped only when nothing it draws would change.
        [[nodiscard]] static std::vector<std::wstring> KeysOf(
            winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::MediaSummary> const& titles);
        [[nodiscard]] bool HasUsableContent() const noexcept;
        void RaiseState();
        void RaiseLayoutMetrics();
        void Raise(wchar_t const* name);
        std::shared_ptr<::HaloDesktop::Shell::LayoutMetricsService> m_layout;
        std::shared_ptr<::HaloDesktop::Services::ICatalogService> m_catalog;
        std::shared_ptr<::HaloDesktop::Services::LibraryService> m_library;
        std::shared_ptr<::HaloDesktop::Services::ISessionService> m_session;
        std::uint64_t m_metricsToken{};
        ::HaloDesktop::Services::CatalogChangedToken m_continueToken{};
        ::HaloDesktop::Services::CatalogChangedToken m_userStateToken{};
        ::HaloDesktop::Services::IdentityChangedToken m_identityToken{};
        std::shared_ptr<::HaloDesktop::Services::NavigationService> m_navigation;
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_featured{ nullptr };
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Shelf> m_sourceShelves{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_continueItems{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_shelves{ nullptr };
        // The titles the recently watched shelf was last built with, so a
        // continue change that leaves them alone does not rebuild every shelf.
        std::vector<std::wstring> m_recentKeys;
        std::int32_t m_filterIndex{};
        std::int32_t m_featuredIndex{};
        std::uint32_t m_loadVersion{};
        std::uint64_t m_appliedVersion{};
        bool m_loading{ true };
        bool m_error{};
        bool m_refreshError{};
        bool m_active{};
        bool m_featuredAutoAdvance{ true };
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
