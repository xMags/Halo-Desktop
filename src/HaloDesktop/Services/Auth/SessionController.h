#pragma once

#include "Services/Auth/ITokenProvider.h"
#include "Services/Auth/SessionStore.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <winrt/Microsoft.UI.Dispatching.h>

namespace HaloDesktop::Services
{
    class QueryCache;
}

namespace HaloDesktop::Services::Auth
{
    class LocalAuthSession;
    class OidcAuthSession;
    class SessionStore;

    enum class SessionKind
    {
        None,
        Local,
        Oidc,
    };

    // Thread-safe session authority. UI cache clearing and rejection callbacks
    // are marshalled to the dispatcher captured at construction.
    class SessionController final : public ITokenProvider
    {
    public:
        using RejectedHandler = std::function<void()>;

        SessionController(
            std::shared_ptr<SessionStore> store,
            std::shared_ptr<LocalAuthSession> localSession,
            std::shared_ptr<OidcAuthSession> oidcSession,
            std::shared_ptr<::HaloDesktop::Services::QueryCache> queryCache,
            winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher);

        [[nodiscard]] concurrency::task<void> RestoreAsync();
        [[nodiscard]] concurrency::task<void> SignInLocalAsync(
            winrt::hstring username,
            winrt::hstring password);
        [[nodiscard]] concurrency::task<void> SignInOidcAsync(StoredOidcSession session);
        [[nodiscard]] concurrency::task<void> SignOutAsync();

        [[nodiscard]] bool IsSignedIn() const noexcept;
        [[nodiscard]] SessionKind Kind() const noexcept;
        void SetRejectedHandler(RejectedHandler handler);

        [[nodiscard]] concurrency::task<std::optional<winrt::hstring>> AccessTokenAsync() override;
        [[nodiscard]] concurrency::task<std::optional<winrt::hstring>> RefreshAccessTokenAsync() override;
        [[nodiscard]] concurrency::task<void> RejectSessionAsync(std::uint64_t expectedGeneration) override;
        [[nodiscard]] std::uint64_t SessionGeneration() const noexcept override;

    private:
        std::shared_ptr<SessionStore> m_store;
        std::shared_ptr<LocalAuthSession> m_localSession;
        std::shared_ptr<OidcAuthSession> m_oidcSession;
        std::shared_ptr<::HaloDesktop::Services::QueryCache> m_queryCache;
        winrt::Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };
        mutable std::mutex m_mutex;
        SessionKind m_kind{ SessionKind::None };
        std::uint64_t m_generation{};
        RejectedHandler m_rejectedHandler;
    };
}
