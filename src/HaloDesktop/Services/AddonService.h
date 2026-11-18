#pragma once

#include "Services/AddonListPolicy.h"
#include "Services/AddonRowPolicy.h"
#include "Services/ServiceInterfaces.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <ppltasks.h>
#include <vector>

namespace HaloDesktop::Api
{
    class ApiClient;
    class BackendSelector;
}

namespace HaloDesktop::Services
{
    class QueryCache;

    // UI-thread-only addon repository. Transport URLs remain in native records;
    // XAML rows expose only display data and opaque IDs. The caller's own list
    // goes through the current backend; the admin's global list is
    // account-only and goes to the account's API client.
    class AddonService final : public IAddonService
    {
    public:
        AddonService(
            std::shared_ptr<::HaloDesktop::Api::ApiClient> apiClient,
            std::shared_ptr<::HaloDesktop::Api::BackendSelector> backends,
            std::shared_ptr<QueryCache> queryCache,
            std::shared_ptr<ISessionService> session);
        ~AddonService() override;

        AddonService(AddonService const&) = delete;
        AddonService& operator=(AddonService const&) = delete;
        AddonService(AddonService&&) = delete;
        AddonService& operator=(AddonService&&) = delete;

        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<winrt::HaloDesktop::Addon> Items() const override;
        [[nodiscard]] std::vector<::HaloDesktop::Api::Dto::AddonRecord> Records() const override;
        [[nodiscard]] bool CanEditLists() const noexcept override;
        [[nodiscard]] concurrency::task<void> LoadAsync() override;
        [[nodiscard]] concurrency::task<void> AddAsync(winrt::hstring transportUrl) override;
        [[nodiscard]] concurrency::task<void> RemoveAsync(winrt::hstring addonId) override;
        [[nodiscard]] concurrency::task<void> SetCatalogsVisibleAsync(
            winrt::hstring addonId,
            bool visible) override;
        void RefreshEditability() override;
        [[nodiscard]] bool HasLoaded() const noexcept override;
        [[nodiscard]] bool CanEditScope(bool global) const noexcept override;
        [[nodiscard]] concurrency::task<void> SaveOrderAsync(
            bool global,
            std::vector<winrt::hstring> orderedIds) override;
        [[nodiscard]] InstallForEveryoneCheck CheckInstallForEveryone(
            winrt::hstring const& addonId) const override;
        [[nodiscard]] concurrency::task<InstallForEveryoneOutcome> InstallForEveryoneAsync(
            winrt::hstring addonId) override;
        void OnAccountChanged();

    private:
        [[nodiscard]] std::vector<AddonListEntry> ListEntries(bool global) const;
        [[nodiscard]] InstallForEveryonePlan PlanInstall(winrt::hstring const& addonId) const;
        [[nodiscard]] concurrency::task<void> LoadCoreAsync(std::uint64_t accountVersion);
        void Apply(::HaloDesktop::Api::Dto::AddonsPayload payload);
        // Editability and the display rows, from the records and the admin flag.
        void RebuildItems();
        [[nodiscard]] std::vector<winrt::hstring> TransportUrls(bool global) const;
        // The global list to the account, the caller's own to the current backend.
        [[nodiscard]] concurrency::task<void> PutListAsync(std::vector<winrt::hstring> transportUrls, bool global);
        [[nodiscard]] concurrency::task<void> PatchListEntryAsync(winrt::hstring addonId, bool global, bool hideCatalogs);

        std::shared_ptr<::HaloDesktop::Api::ApiClient> m_apiClient;
        std::shared_ptr<::HaloDesktop::Api::BackendSelector> m_backends;
        std::shared_ptr<QueryCache> m_queryCache;
        std::shared_ptr<ISessionService> m_session;
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::HaloDesktop::Addon> m_items{ nullptr };
        std::vector<::HaloDesktop::Api::Dto::AddonRecord> m_records;
        std::optional<concurrency::task<void>> m_loadTask;
        std::optional<concurrency::task<void>> m_mutationTail;
        AddonListEditability m_editability{};
        // The admin flag m_editability and m_items were built with.
        bool m_builtAsAdmin{};
        // Whether this account's list has been read and applied.
        bool m_listApplied{};
        IdentityChangedToken m_profileChangedToken{};
        bool m_seedAttempted{};
        std::uint64_t m_accountVersion{};
        std::uint64_t m_loadTaskAccountVersion{};
    };
}
