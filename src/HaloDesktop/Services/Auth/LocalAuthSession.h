#pragma once

#include "Api/Dto.h"
#include "Services/Auth/SessionStore.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <pplawait.h>
#include <ppltasks.h>
#include <winrt/Windows.Foundation.h>

namespace HaloDesktop::Api
{
    class HttpExecutor;
}

namespace HaloDesktop::Services::Auth
{
    // Thread-safe local-mode token owner with expiry-band refresh, single-flight
    // network work, and revision-safe persistence.
    class LocalAuthSession final
    {
    public:
        LocalAuthSession(
            winrt::hstring baseUrl,
            std::shared_ptr<::HaloDesktop::Api::HttpExecutor> executor,
            std::shared_ptr<SessionStore> store);

        void Restore(StoredLocalSession session);
        [[nodiscard]] concurrency::task<::HaloDesktop::Api::Dto::IssuedToken> LoginAsync(
            winrt::hstring username,
            winrt::hstring password);
        [[nodiscard]] concurrency::task<std::optional<winrt::hstring>> AccessTokenAsync();
        [[nodiscard]] concurrency::task<std::optional<winrt::hstring>> RefreshAccessTokenAsync();
        [[nodiscard]] concurrency::task<void> ClearAsync();
        [[nodiscard]] concurrency::task<bool> ClearIfRevisionAsync(std::uint64_t expectedRevision);
        [[nodiscard]] std::uint64_t Revision() const noexcept;

    private:
        struct RefreshFlight final
        {
            std::uint64_t Id{};
            std::shared_ptr<concurrency::task<std::optional<winrt::hstring>>> Task;
        };

        [[nodiscard]] concurrency::task<std::optional<winrt::hstring>> RefreshNowAsync(
            StoredLocalSession observed,
            std::uint64_t observedRevision);
        [[nodiscard]] concurrency::task<void> PersistUntilCurrentAsync(
            std::uint64_t revision,
            std::optional<StoredLocalSession> session);
        [[nodiscard]] winrt::Windows::Foundation::Uri Endpoint(wchar_t const* path) const;
        void ClearFlight(std::uint64_t flightId) noexcept;

        winrt::hstring m_baseUrl;
        std::shared_ptr<::HaloDesktop::Api::HttpExecutor> m_executor;
        std::shared_ptr<SessionStore> m_store;
        mutable std::mutex m_mutex;
        std::optional<StoredLocalSession> m_session;
        std::optional<RefreshFlight> m_refreshFlight;
        std::uint64_t m_revision{};
        std::uint64_t m_nextFlightId{};
    };
}
