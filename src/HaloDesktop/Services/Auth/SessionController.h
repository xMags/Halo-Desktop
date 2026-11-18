#pragma once

#include "Services/Auth/ITokenProvider.h"
#include "Services/Auth/SessionStore.h"
#include "Services/Auth/SignInAddon.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
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
    class SignInAddonRegistry;

    enum class SessionKind
    {
        None,
        Local,
        Oidc,
        // A sign-in add-on's session, `addon:<mode>`.
        Addon,
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
            std::shared_ptr<SignInAddonRegistry const> addons,
            std::shared_ptr<::HaloDesktop::Services::QueryCache> queryCache,
            winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher);

        // forgetBuiltInSessions: the sessions saved on this PC were saved for
        // another built-in server. Browser and add-on sessions are then wiped
        // here only (signing out of that server is not this build's call), and
        // a username and password session is kept only when it names its own
        // server.
        [[nodiscard]] concurrency::task<void> RestoreAsync(bool forgetBuiltInSessions);
        // serverUrl is the server the session is opened on: the built-in one,
        // or a self-hosted server typed into the sign-in form.
        [[nodiscard]] concurrency::task<std::uint64_t> SignInLocalAsync(
            winrt::hstring serverUrl,
            winrt::hstring username,
            winrt::hstring password);
        [[nodiscard]] concurrency::task<std::uint64_t> SignInOidcAsync(StoredOidcSession session);
        [[nodiscard]] concurrency::task<std::uint64_t> SignInAddonAsync(
            std::shared_ptr<ISignInAddon> addon,
            winrt::hstring serverUrl,
            AddonAuthConfig config,
            SignInOpenedHandler onOpened,
            std::stop_token cancellation);
        [[nodiscard]] concurrency::task<void> SignOutAsync();

        [[nodiscard]] bool IsSignedIn() const noexcept;
        [[nodiscard]] SessionKind Kind() const noexcept;
        // "local", "oidc", `addon:<mode>`, or empty while signed out.
        [[nodiscard]] std::wstring KindName() const;
        void SetRejectedHandler(RejectedHandler handler);

        [[nodiscard]] concurrency::task<std::optional<winrt::hstring>> AccessTokenAsync() override;
        [[nodiscard]] concurrency::task<std::optional<winrt::hstring>> RefreshAccessTokenAsync() override;
        [[nodiscard]] concurrency::task<void> RejectSessionAsync(std::uint64_t expectedGeneration) override;
        [[nodiscard]] std::uint64_t SessionGeneration() const noexcept override;
        [[nodiscard]] SessionTarget CurrentTarget() const override;

    private:
        [[nodiscard]] std::shared_ptr<ISignInAddon> ActiveAddon() const;

        std::shared_ptr<SessionStore> m_store;
        std::shared_ptr<LocalAuthSession> m_localSession;
        std::shared_ptr<OidcAuthSession> m_oidcSession;
        std::shared_ptr<SignInAddonRegistry const> m_addons;
        std::shared_ptr<::HaloDesktop::Services::QueryCache> m_queryCache;
        winrt::Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };
        mutable std::mutex m_mutex;
        SessionKind m_kind{ SessionKind::None };
        std::shared_ptr<ISignInAddon> m_activeAddon;
        std::uint64_t m_generation{};
        RejectedHandler m_rejectedHandler;
    };
}
