#pragma once
#include "HomeViewModel.g.h"
#include "Services/AppServices.h"
#include "Services/HomeShelfLoadPolicy.h"
#include "Services/ServiceInterfaces.h"
#include <cstddef>
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
        // The hero's skeleton: while the shelf list itself loads, and then until
        // the shelves that have answered fix the carousel's picks.
        [[nodiscard]] Microsoft::UI::Xaml::Visibility FeaturedSkeletonVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility RefreshErrorVisibility() const noexcept;
        [[nodiscard]] winrt::hstring ContinueCountLabel() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ContinueVisibility() const noexcept;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable ContinueItems() const;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable Shelves() const;
        [[nodiscard]] auto ContinueItemsView() const { return m_continueItems; }
        [[nodiscard]] auto ShelvesView() const { return m_shelves; }
        [[nodiscard]] std::int32_t FilterIndex() const noexcept;
        // The page itself: its rows, or the skeletons standing in for them.
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ContentVisibility() const noexcept;
        // The skeleton shelves shown while the shelf list itself loads.
        [[nodiscard]] Microsoft::UI::Xaml::Visibility LoadingVisibility() const noexcept;
        // Not projected: whether any skeleton is on screen, so the view runs
        // their shared pulse only then.
        [[nodiscard]] bool SkeletonsShown() const noexcept;
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
        // A sideways strip row runs past the right gutter to the window's edge,
        // so cards slide out under the edge instead of being cut at the gutter.
        // The row keeps the left gutter; its header keeps the right one, and its
        // strip ends a gutter in, so the last card still stops on the gutter line.
        [[nodiscard]] Microsoft::UI::Xaml::Thickness StripRowPadding() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Thickness StripHeaderMargin() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Thickness StripEndInset() const noexcept;
        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;
    private:
        // The layout-step gutter, shared by ContentPadding and HeaderPadding so
        // the title and the content under it can never drift apart.
        [[nodiscard]] double Gutter() const noexcept;
        winrt::Windows::Foundation::IAsyncAction LoadAsync();
        winrt::Windows::Foundation::IAsyncAction ToggleFeaturedLibraryAsync(winrt::HaloDesktop::FeaturedItem item);
        // Takes whatever the catalog now holds: the rows, the shelf list and
        // every shelf that has landed. Unchanged rows are left alone.
        void AdoptHome();
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
        // A load failed, or every catalog shelf did. Not while a load runs.
        [[nodiscard]] bool Failed() const;
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
        ::HaloDesktop::Services::CatalogChangedToken m_homeToken{};
        ::HaloDesktop::Services::IdentityChangedToken m_identityToken{};
        std::shared_ptr<::HaloDesktop::Services::NavigationService> m_navigation;
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_featured{ nullptr };
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Shelf> m_sourceShelves{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_continueItems{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_shelves{ nullptr };
        // What each row of m_shelves is and draws, index for index.
        std::vector<::HaloDesktop::Services::HomeRowIdentity> m_rowIdentities;
        // How many of those rows are a shelf still loading.
        std::size_t m_loadingRows{};
        // The continue row the strip was last filled from, so a shelf landing
        // does not refill an unchanged strip.
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::ContinueItem> m_continueSource{ nullptr };
        // The titles the recently watched shelf was last built with, so a
        // continue change that leaves them alone does not rebuild every shelf.
        std::vector<std::wstring> m_recentKeys;
        std::int32_t m_filterIndex{};
        std::int32_t m_featuredIndex{};
        std::uint32_t m_loadVersion{};
        std::uint64_t m_appliedVersion{};
        bool m_loading{ true };
        bool m_loadFailed{};
        bool m_featuredSettled{};
        bool m_active{};
        bool m_featuredAutoAdvance{ true };
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
