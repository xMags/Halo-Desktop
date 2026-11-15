#pragma once

#include "Services/CatalogRefreshPolicy.h"
#include "Services/ContinueShelfPolicy.h"
#include "Services/HomeShelfLoadPolicy.h"
#include "Services/ServiceInterfaces.h"

#include "Api/Dto.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace HaloDesktop::Api { class ApiClient; }
namespace HaloDesktop::Services
{
    class AddonService;
    class ContinueArtworkService;
    class ContinueNextEpisodeService;
    class LibraryService;
    class SettingsSyncService;
    class WatchStateService;

    // UI-thread-only catalog aggregator. API calls run on background threads;
    // their answers are applied on the UI thread, with no await between reading
    // an answer's ticket and publishing it. The library and continue rows are
    // published as soon as their own data is in; Home's catalog shelves are laid
    // out once the addons are known and then land one at a time, each guarded by
    // its own generation (HomeShelfBoard) and by the account version.
    class CatalogService final : public ICatalogService, public std::enable_shared_from_this<CatalogService>
    {
    public:
        CatalogService(
            std::shared_ptr<::HaloDesktop::Api::ApiClient> apiClient,
            std::shared_ptr<AddonService> addons,
            std::shared_ptr<LibraryService> library,
            std::shared_ptr<WatchStateService> watchState,
            std::shared_ptr<ContinueArtworkService> artwork,
            std::shared_ptr<ContinueNextEpisodeService> nextEpisodes,
            std::shared_ptr<SettingsSyncService> settings);

        [[nodiscard]] concurrency::task<void> LoadAsync() override;
        [[nodiscard]] bool HasLoaded() const override;
        [[nodiscard]] std::uint64_t SnapshotVersion() const noexcept override;
        [[nodiscard]] HomeCatalogStatus CatalogStatus() const override;
        [[nodiscard]] CatalogChangedToken AddHomeChangedHandler(CatalogChangedHandler handler) override;
        void RemoveHomeChangedHandler(CatalogChangedToken token) noexcept override;
        void InvalidateCatalogs() noexcept override;
        [[nodiscard]] bool CatalogsDirty() const noexcept override;
        [[nodiscard]] concurrency::task<void> RefreshCatalogsIfDirtyAsync() override;
        void RefreshContinue() override;
        void RebuildLibrary() override;
        [[nodiscard]] winrt::HaloDesktop::MediaSummary Hero() const override;
        [[nodiscard]] winrt::HaloDesktop::MediaSummary FeaturedForFilter(
            std::int32_t filterIndex) const override;
        [[nodiscard]] FeaturedSelection FeaturedForHome(std::int32_t filterIndex, std::uint32_t count) const override;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::ContinueItem> ContinueWatching() const override;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Shelf> Shelves() const override;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::MediaSummary> LibraryItems() const override;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::MediaSummary>
            RecentlyWatchedForFilter(std::int32_t filterIndex) const override;
        [[nodiscard]] concurrency::task<std::vector<winrt::HaloDesktop::MediaSummary>> FetchCatalogPageAsync(
            std::shared_ptr<CatalogPagingSource const> source,
            std::size_t skip) override;

        [[nodiscard]] CatalogChangedToken AddContinueChangedHandler(
            CatalogChangedHandler handler) override;
        void RemoveContinueChangedHandler(CatalogChangedToken token) noexcept override;
        void PublishUserStateChange() override;
        [[nodiscard]] CatalogChangedToken AddUserStateChangedHandler(
            CatalogChangedHandler handler) override;
        void RemoveUserStateChangedHandler(CatalogChangedToken token) noexcept override;
        [[nodiscard]] concurrency::task<std::optional<SimilarShelf>> FetchSimilarAsync(
            winrt::hstring type,
            winrt::hstring metaId,
            std::vector<winrt::hstring> genres) override;
        void OnAccountChanged();

    private:
        // One catalog shelf's request, as laid out on the board.
        struct ShelfQuery final
        {
            winrt::hstring AddonId;
            winrt::hstring AddonName;
            ::HaloDesktop::Api::Dto::AddonRecord::Catalog Catalog;
            // What the catalog is asked with: the account's language, for a
            // language catalog that offers it.
            std::vector<std::pair<winrt::hstring, winrt::hstring>> Extras;
            std::wstring Key;
        };

        [[nodiscard]] concurrency::task<void> RunSharedRefreshAsync(bool loadDependencies);
        [[nodiscard]] concurrency::task<void> LoadCoreAsync(
            bool loadDependencies,
            std::uint64_t accountVersion);
        // The library, library shelf and continue row from the rows the library
        // and watch-state services now hold.
        void PublishUserRows();
        [[nodiscard]] std::vector<ShelfQuery> BuildShelfQueries(std::optional<std::wstring_view> language) const;
        void StartShelfFetches(std::vector<HomeShelfTicket> tickets);
        // Awaits one shelf's request off the UI thread and hands its answer to
        // the service back on it. Static so no `this` outlives the service: the
        // continuation reaches it only through the weak reference.
        static concurrency::task<void> RunShelfFetchAsync(
            std::weak_ptr<CatalogService> weak,
            winrt::apartment_context uiContext,
            concurrency::task<std::vector<::HaloDesktop::Api::Dto::MetaPreview>> request,
            HomeShelfTicket ticket,
            std::uint64_t accountVersion);
        void ApplyShelfAnswer(
            HomeShelfTicket const& ticket,
            std::optional<std::vector<::HaloDesktop::Api::Dto::MetaPreview>> answer);
        [[nodiscard]] winrt::HaloDesktop::Shelf BuildCatalogShelf(
            ShelfQuery const& query,
            std::vector<::HaloDesktop::Api::Dto::MetaPreview> const* items) const;
        // Re-lays the catalog shelves from the board and the stored answers,
        // then the full shelf list with the library shelf ahead of them.
        void ComposeCatalogShelves();
        void NotifyHomeChanged();
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
        static void InvokeHandlers(
            std::unordered_map<CatalogChangedToken, CatalogChangedHandler> const& registered);
        // The language the account currently asks language catalogs for.
        [[nodiscard]] std::optional<std::wstring> CurrentCatalogLanguage() const;

        std::shared_ptr<::HaloDesktop::Api::ApiClient> m_apiClient;
        std::shared_ptr<AddonService> m_addons;
        std::shared_ptr<LibraryService> m_library;
        std::shared_ptr<WatchStateService> m_watchState;
        std::shared_ptr<ContinueArtworkService> m_artwork;
        std::shared_ptr<ContinueNextEpisodeService> m_nextEpisodes;
        std::shared_ptr<SettingsSyncService> m_settings;
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::ContinueItem> m_continue{ nullptr };
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Shelf> m_catalogShelves{ nullptr };
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Shelf> m_shelves{ nullptr };
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::MediaSummary> m_libraryItems{ nullptr };
        std::optional<concurrency::task<void>> m_loadTask;
        CatalogRefreshState m_refreshState;
        std::uint64_t m_snapshotVersion{};
        std::uint64_t m_accountVersion{};
        std::uint64_t m_loadTaskAccountVersion{};
        // The catalog language the laid-out shelves were asked with. A
        // different one makes the catalogs dirty without anything else having
        // changed, and only the catalogs it affects are fetched again.
        std::optional<std::wstring> m_appliedLanguage;
        // Home's catalog shelves: their load state in shelf order, the request
        // behind each, and the titles each last answered with, by request key.
        HomeShelfBoard m_shelfBoard;
        std::vector<ShelfQuery> m_shelfQueries;
        std::unordered_map<std::wstring, std::vector<::HaloDesktop::Api::Dto::MetaPreview>> m_shelfAnswers;
        // The shelf built from each stored answer, so a shelf landing rebuilds
        // only itself. Dropped with its answer.
        std::unordered_map<std::wstring, winrt::HaloDesktop::Shelf> m_builtShelves;
        std::unordered_map<CatalogChangedToken, CatalogChangedHandler> m_homeHandlers;
        CatalogChangedToken m_nextHomeToken{ 1 };
        std::unordered_map<CatalogChangedToken, CatalogChangedHandler> m_continueHandlers;
        CatalogChangedToken m_nextContinueToken{ 1 };
        std::unordered_map<CatalogChangedToken, CatalogChangedHandler> m_userStateHandlers;
        CatalogChangedToken m_nextUserStateToken{ 1 };
    };
}
