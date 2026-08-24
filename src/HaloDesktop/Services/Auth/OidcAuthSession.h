#pragma once

#include "Services/Auth/SessionStore.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <pplawait.h>
#include <ppltasks.h>

namespace HaloDesktop::Api
{
    class HttpExecutor;
}

namespace HaloDesktop::Services::Auth
{
    // Thread-safe OIDC token owner with rotating single-flight refresh and
    // invalid_grant-only session death.
    class OidcAuthSession final
    {
    public:
        OidcAuthSession(
            std::shared_ptr<::HaloDesktop::Api::HttpExecutor> executor,
            std::shared_ptr<SessionStore> store);

        void Restore(StoredOidcSession session);
        [[nodiscard]] concurrency::task<void> EstablishAsync(StoredOidcSession session);
        [[nodiscard]] concurrency::task<std::optional<winrt::hstring>> AccessTokenAsync();
        [[nodiscard]] concurrency::task<std::optional<winrt::hstring>> RefreshAccessTokenAsync();
        [[nodiscard]] concurrency::task<void> SignOutAsync(bool endIdentityProviderSession);
        [[nodiscard]] concurrency::task<bool> ClearIfRevisionAsync(std::uint64_t expectedRevision);
        [[nodiscard]] std::uint64_t Revision() const noexcept;

    private:
        struct RefreshFlight final
        {
            std::uint64_t Id{};
            std::shared_ptr<concurrency::task<std::optional<winrt::hstring>>> Task;
        };

        [[nodiscard]] concurrency::task<std::optional<winrt::hstring>> RefreshNowAsync(
            StoredOidcSession observed,
            std::uint64_t observedRevision);
        [[nodiscard]] concurrency::task<void> PersistUntilCurrentAsync(
            std::uint64_t revision,
            std::optional<StoredOidcSession> session);
        void ClearFlight(std::uint64_t flightId) noexcept;

        std::shared_ptr<::HaloDesktop::Api::HttpExecutor> m_executor;
        std::shared_ptr<SessionStore> m_store;
        mutable std::mutex m_mutex;
        std::optional<StoredOidcSession> m_session;
        std::optional<RefreshFlight> m_refreshFlight;
        std::uint64_t m_revision{};
        std::uint64_t m_nextFlightId{};
    };
}
