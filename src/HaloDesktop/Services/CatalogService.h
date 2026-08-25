#pragma once

#include "Services/ServiceInterfaces.h"

#include <memory>
#include <optional>
#include <vector>

namespace HaloDesktop::Api { class ApiClient; }
namespace HaloDesktop::Services
{
    class AddonService;
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
            std::shared_ptr<WatchStateService> watchState);

        [[nodiscard]] concurrency::task<void> LoadAsync() override;
        [[nodiscard]] concurrency::task<void> SearchAsync(winrt::hstring query) override;
        [[nodiscard]] winrt::HaloDesktop::MediaSummary Hero() const override;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::ContinueItem> ContinueWatching() const override;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Shelf> Shelves() const override;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::MediaSummary> LibraryItems() const override;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::SearchGroup> SearchResults() const override;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IVectorView<winrt::hstring> RecentTerms() const override;

        void RecordRecent(winrt::hstring term) override;

    private:
        [[nodiscard]] concurrency::task<void> LoadCoreAsync();
        void BuildLibraryAndContinue();
        void LoadRecentTerms();
        void SaveRecentTerms();

        std::shared_ptr<::HaloDesktop::Api::ApiClient> m_apiClient;
        std::shared_ptr<AddonService> m_addons;
        std::shared_ptr<LibraryService> m_library;
        std::shared_ptr<WatchStateService> m_watchState;
        winrt::HaloDesktop::MediaSummary m_hero{ nullptr };
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::ContinueItem> m_continue{ nullptr };
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Shelf> m_shelves{ nullptr };
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::MediaSummary> m_libraryItems{ nullptr };
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::SearchGroup> m_searchResults{ nullptr };
        winrt::Windows::Foundation::Collections::IVectorView<winrt::hstring> m_recentTerms{ nullptr };
        std::optional<concurrency::task<void>> m_loadTask;
        std::uint64_t m_searchVersion{};
    };
}
