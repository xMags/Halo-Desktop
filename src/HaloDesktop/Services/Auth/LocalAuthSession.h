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
    // Thread-safe username and password session owner with expiry-band
    // refresh, single-flight network work, and revision-safe persistence. A
    // session belongs to the server it was opened on: its token is refreshed
    // there and every request made with it goes there. The built-in server
    // given here is only the owner of sessions saved before they recorded
    // their server.
    class LocalAuthSession final
    {
    public:
        LocalAuthSession(
            winrt::hstring builtInServerUrl,
            std::shared_ptr<::HaloDesktop::Api::HttpExecutor> executor,
            std::shared_ptr<SessionStore> store);

        void Restore(StoredLocalSession session);
        // Posts the credentials to serverUrl's /auth/login, without following a
        // redirect, and saves the session it issues. The password is used for
        // this one request and kept nowhere.
        [[nodiscard]] concurrency::task<::HaloDesktop::Api::Dto::IssuedToken> LoginAsync(
            winrt::hstring serverUrl,
            winrt::hstring username,
            winrt::hstring password);
        [[nodiscard]] concurrency::task<std::optional<winrt::hstring>> AccessTokenAsync();
        [[nodiscard]] concurrency::task<std::optional<winrt::hstring>> RefreshAccessTokenAsync();
        [[nodiscard]] concurrency::task<void> ClearAsync();
        [[nodiscard]] concurrency::task<bool> ClearIfRevisionAsync(std::uint64_t expectedRevision);
        [[nodiscard]] std::uint64_t Revision() const noexcept;
        // The server the current session talks to; null without a session.
        [[nodiscard]] std::optional<winrt::hstring> ServerUrl() const;

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
        void ClearFlight(std::uint64_t flightId) noexcept;

        winrt::hstring m_builtInServerUrl;
        std::shared_ptr<::HaloDesktop::Api::HttpExecutor> m_executor;
        std::shared_ptr<SessionStore> m_store;
        mutable std::mutex m_mutex;
        std::optional<StoredLocalSession> m_session;
        std::optional<RefreshFlight> m_refreshFlight;
        std::uint64_t m_revision{};
        std::uint64_t m_nextFlightId{};
    };
}
