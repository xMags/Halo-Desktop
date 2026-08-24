#pragma once

#include "Api/Dto.h"
#include "Services/ServiceInterfaces.h"

#include <chrono>
#include <functional>
#include <memory>
#include <optional>

namespace HaloDesktop::Api
{
    class ApiClient;
}

namespace HaloDesktop::Services::Auth
{
    class SessionStore;
    class SessionController;
    class OidcSignInFlow;
}

namespace HaloDesktop::Services
{
    class NavigationService;

    // UI-thread-only facade over the thread-safe session authority. Restore is
    // a local DPAPI read; identity hydration is deliberately separate so an
    // offline launch never waits for the network.
    class SessionService final : public ISessionService
    {
    public:
        SessionService(
            std::shared_ptr<::HaloDesktop::Api::ApiClient> apiClient,
            std::shared_ptr<Auth::SessionController> controller,
            std::shared_ptr<Auth::SessionStore> store,
            std::shared_ptr<Auth::OidcSignInFlow> oidcSignInFlow,
            std::shared_ptr<NavigationService> navigation);

        [[nodiscard]] winrt::hstring ServerUrl() const override;
        [[nodiscard]] winrt::hstring UserId() const override;
        [[nodiscard]] winrt::hstring UserName() const override;
        [[nodiscard]] bool IsSignedIn() const noexcept override;
        [[nodiscard]] bool IsAdmin() const noexcept override;
        [[nodiscard]] AuthenticationMode Mode() const noexcept override;
        [[nodiscard]] std::uint64_t Generation() const noexcept override;
        [[nodiscard]] concurrency::task<void> RestoreAsync() override;
        [[nodiscard]] concurrency::task<AuthenticationMode> DiscoverAuthenticationAsync() override;
        [[nodiscard]] concurrency::task<winrt::HaloDesktop::SignInOutcome>
            SignInLocalAsync(winrt::hstring username, winrt::hstring password) override;
        [[nodiscard]] concurrency::task<winrt::HaloDesktop::SignInOutcome>
            RequestBrowserSignInAsync() override;
        [[nodiscard]] concurrency::task<std::optional<std::chrono::milliseconds>>
            ProbeHealthAsync() override;
        [[nodiscard]] concurrency::task<void> SignOutAsync() override;

        [[nodiscard]] concurrency::task<void> RefreshIdentityAsync();
        void HandleSessionRejected();
        void SetIdentityChangedHandler(std::function<void()> handler);

    private:
        void ClearIdentity();
        void NotifyIdentityChanged();

        std::shared_ptr<::HaloDesktop::Api::ApiClient> m_apiClient;
        std::shared_ptr<Auth::SessionController> m_controller;
        std::shared_ptr<Auth::SessionStore> m_store;
        std::shared_ptr<Auth::OidcSignInFlow> m_oidcSignInFlow;
        std::shared_ptr<NavigationService> m_navigation;
        std::optional<::HaloDesktop::Api::Dto::AuthConfig> m_oidcConfig;
        winrt::hstring m_userId;
        winrt::hstring m_userName;
        std::function<void()> m_identityChanged;
        AuthenticationMode m_mode{ AuthenticationMode::Unknown };
        bool m_isAdmin{};
    };
}
