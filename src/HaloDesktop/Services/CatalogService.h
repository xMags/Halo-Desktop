#pragma once

#include "Services/CatalogRefreshPolicy.h"
#include "Services/ContinueShelfPolicy.h"
#include "Services/ServiceInterfaces.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace HaloDesktop::Api { class ApiClient; }
namespace HaloDesktop::Services
{
    class AddonService;
    class ContinueArtworkService;
    class ContinueNextEpisodeService;
    class DevicePreferencesStore;
    class LibraryService;
    class WatchStateService;

    // UI-thread-only catalog aggregator. API calls run on background threads;
    // complete snapshots are applied only after the issuing request returns.
    class CatalogService final : public ICatalogService, public std::enable_shared_from_this<CatalogService>
    {
    public:
        CatalogService(
            std::shared_ptr<::HaloDesktop::Api::ApiClient> apiClient,
            std::shared_ptr<AddonService> addons,
            std::shared_ptr<LibraryService> library,
            std::shared_ptr<WatchStateService> watchState,
            std::shared_ptr<DevicePreferencesStore> preferences,
            std::shared_ptr<ContinueArtworkService> artwork,
            std::shared_ptr<ContinueNextEpisodeService> nextEpisodes);

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
        [[nodiscard]] CatalogChangedToken AddContinueChangedHandler(
            CatalogChangedHandler handler) override;
        void RemoveContinueChangedHandler(CatalogChangedToken token) noexcept override;
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
        struct ContinueSnapshot final
        {
            winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::ContinueItem> Items{ nullptr };
            // Series whose newest episode is finished and whose successor is not
            // known yet. Empty once the lookups behind them have landed.
            std::vector<ContinueNextRequest> Requests;
        };

        [[nodiscard]] ContinueSnapshot BuildContinueSnapshot() const;
        // startFill is cleared when the rebuild was itself caused by a fill
        // completing, which is what keeps a lookup that answered from starting
        // another round for the ones that did not.
        void ApplyContinueSnapshot(bool startFill);
        void BuildContinue();
        // Detached on purpose: the shelf is committed and on screen, and each card
        // takes its own still as that item's meta returns.
        void BeginContinueArtworkFill();
        // Detached for the same reason. The shelf gains its promoted cards when
        // these land rather than waiting on an addon before it can be shown.
        void BeginContinueNextFill(std::vector<ContinueNextRequest> requests);
        void NotifyContinueChanged();
        void LoadRecentTerms();
        void SaveRecentTerms();

        std::shared_ptr<::HaloDesktop::Api::ApiClient> m_apiClient;
        std::shared_ptr<AddonService> m_addons;
        std::shared_ptr<LibraryService> m_library;
        std::shared_ptr<WatchStateService> m_watchState;
        std::shared_ptr<DevicePreferencesStore> m_preferences;
        std::shared_ptr<ContinueArtworkService> m_artwork;
        std::shared_ptr<ContinueNextEpisodeService> m_nextEpisodes;
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
        std::unordered_map<CatalogChangedToken, CatalogChangedHandler> m_continueHandlers;
        CatalogChangedToken m_nextContinueToken{ 1 };
    };
}
