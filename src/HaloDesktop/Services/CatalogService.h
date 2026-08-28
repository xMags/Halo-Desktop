#pragma once

#include "Services/CatalogRefreshPolicy.h"
#include "Services/ServiceInterfaces.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace HaloDesktop::Api { class ApiClient; }
namespace HaloDesktop::Services
{
    class AddonService;
    class ContinueArtworkService;
    class DevicePreferencesStore;
    class LibraryService;
    class WatchStateService;

    // UI-thread-only catalog aggregator. API calls run on background threads;
    // complete snapshots are applied only after the issuing request returns.
    class CatalogService final : public ICatalogService
    {
    public:
        CatalogService(
            std::shared_ptr<::HaloDesktop::Api::ApiClient> apiClient,
            std::shared_ptr<AddonService> addons,
            std::shared_ptr<LibraryService> library,
            std::shared_ptr<WatchStateService> watchState,
            std::shared_ptr<DevicePreferencesStore> preferences,
            std::shared_ptr<ContinueArtworkService> artwork);

        [[nodiscard]] concurrency::task<void> LoadAsync() override;
        [[nodiscard]] bool HasLoaded() const override;
        [[nodiscard]] std::uint64_t SnapshotVersion() const noexcept override;
        void InvalidateCatalogs() noexcept override;
        [[nodiscard]] bool CatalogsDirty() const noexcept override;
        [[nodiscard]] concurrency::task<void> RefreshCatalogsIfDirtyAsync() override;
        void RefreshContinue() override;
        void RebuildLibrary() override;
        [[nodiscard]] concurrency::task<void> SearchAsync(winrt::hstring query) override;
        [[nodiscard]] winrt::HaloDesktop::MediaSummary Hero() const override;
        [[nodiscard]] winrt::HaloDesktop::MediaSummary FeaturedForFilter(
            std::int32_t filterIndex) const override;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::MediaSummary>
            FeaturedSetForFilter(std::int32_t filterIndex, std::uint32_t count) const override;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::ContinueItem> ContinueWatching() const override;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Shelf> Shelves() const override;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::MediaSummary> LibraryItems() const override;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::SearchGroup> SearchResults() const override;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IVectorView<winrt::hstring> RecentTerms() const override;

        void RecordRecent(winrt::hstring term) override;
        void OnAccountChanged();

    private:
        [[nodiscard]] concurrency::task<void> RunSharedRefreshAsync(bool loadDependencies);
        [[nodiscard]] concurrency::task<void> LoadCoreAsync(
            bool loadDependencies,
            std::uint64_t accountVersion);
        [[nodiscard]] std::vector<winrt::HaloDesktop::MediaSummary> BuildLibraryItems() const;
        [[nodiscard]] std::vector<winrt::HaloDesktop::Shelf> BuildShelves(
            winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Shelf> const& catalogShelves,
            winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::MediaSummary> const& libraryItems) const;
        [[nodiscard]] std::vector<winrt::HaloDesktop::ContinueItem> BuildContinueItems() const;
        void BuildContinue();
        // Detached on purpose: the shelf is committed and on screen, and each card
        // takes its own still as that item's meta returns.
        void BeginContinueArtworkFill();
        void LoadRecentTerms();
        void SaveRecentTerms();

        std::shared_ptr<::HaloDesktop::Api::ApiClient> m_apiClient;
        std::shared_ptr<AddonService> m_addons;
        std::shared_ptr<LibraryService> m_library;
        std::shared_ptr<WatchStateService> m_watchState;
        std::shared_ptr<DevicePreferencesStore> m_preferences;
        std::shared_ptr<ContinueArtworkService> m_artwork;
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::ContinueItem> m_continue{ nullptr };
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Shelf> m_catalogShelves{ nullptr };
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Shelf> m_shelves{ nullptr };
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::MediaSummary> m_libraryItems{ nullptr };
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::SearchGroup> m_searchResults{ nullptr };
        winrt::Windows::Foundation::Collections::IVectorView<winrt::hstring> m_recentTerms{ nullptr };
        std::optional<concurrency::task<void>> m_loadTask;
        CatalogRefreshState m_refreshState;
        std::uint64_t m_searchVersion{};
        std::uint64_t m_snapshotVersion{};
        std::uint64_t m_accountVersion{};
        std::uint64_t m_loadTaskAccountVersion{};
    };
}
