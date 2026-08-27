#pragma once

#include "Services/ServiceInterfaces.h"

#include <cstdint>
#include <memory>
#include <optional>

namespace HaloDesktop::Api
{
    class ApiClient;
}

namespace HaloDesktop::Services
{
    class QueryCache;

    // UI-thread-only addon repository. Transport URLs remain in native records;
    // XAML rows expose only display data and opaque IDs.
    class AddonService final : public IAddonService
    {
    public:
        AddonService(
            std::shared_ptr<::HaloDesktop::Api::ApiClient> apiClient,
            std::shared_ptr<QueryCache> queryCache,
            std::shared_ptr<ISessionService> session);

        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<winrt::HaloDesktop::Addon> Items() const override;
        [[nodiscard]] std::vector<::HaloDesktop::Api::Dto::AddonRecord> Records() const override;
        [[nodiscard]] bool CanEditLists() const noexcept override;
        [[nodiscard]] concurrency::task<void> LoadAsync() override;
        [[nodiscard]] concurrency::task<void> AddAsync(winrt::hstring transportUrl) override;
        [[nodiscard]] concurrency::task<void> RemoveAsync(winrt::hstring addonId) override;
        [[nodiscard]] concurrency::task<void> SetCatalogsVisibleAsync(
            winrt::hstring addonId,
            bool visible) override;
        void OnAccountChanged();

    private:
        [[nodiscard]] concurrency::task<void> LoadCoreAsync(std::uint64_t accountVersion);
        void Apply(::HaloDesktop::Api::Dto::AddonsPayload payload);
        [[nodiscard]] std::vector<winrt::hstring> TransportUrls(bool global) const;

        std::shared_ptr<::HaloDesktop::Api::ApiClient> m_apiClient;
        std::shared_ptr<QueryCache> m_queryCache;
        std::shared_ptr<ISessionService> m_session;
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::HaloDesktop::Addon> m_items{ nullptr };
        std::vector<::HaloDesktop::Api::Dto::AddonRecord> m_records;
        std::optional<concurrency::task<void>> m_loadTask;
        bool m_canEditLists{};
        bool m_seedAttempted{};
        std::uint64_t m_accountVersion{};
        std::uint64_t m_loadTaskAccountVersion{};
    };
}
