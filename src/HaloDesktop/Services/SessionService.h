#pragma once

#include "Api/Dto.h"
#include "Services/ServiceInterfaces.h"

#include <chrono>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <unordered_map>

namespace HaloDesktop::Api
{
    class ApiClient;
}

namespace HaloDesktop::Services::Auth
{
    class SessionStore;
    class SessionController;
    class OidcSignInFlow;
    class SignInAddonRegistry;
    class ISignInAddon;
}

namespace HaloDesktop::Services
{
    class DevicePreferencesStore;
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
            std::shared_ptr<Auth::SignInAddonRegistry const> addons,
            std::shared_ptr<DevicePreferencesStore> preferences,
            std::shared_ptr<NavigationService> navigation);

        [[nodiscard]] winrt::hstring ServerUrl() const override;
        [[nodiscard]] winrt::hstring UserId() const override;
        [[nodiscard]] winrt::hstring UserName() const override;
        [[nodiscard]] bool IsSignedIn() const noexcept override;
        [[nodiscard]] bool IsAdmin() const noexcept override;
        [[nodiscard]] std::optional<winrt::hstring> Plan() const override;
        [[nodiscard]] std::optional<winrt::hstring> AvatarVersion() const override;
        [[nodiscard]] std::uint64_t Generation() const noexcept override;
        [[nodiscard]] concurrency::task<void> RestoreAsync() override;
        [[nodiscard]] concurrency::task<SignInDiscovery> DiscoverAuthenticationAsync() override;
        [[nodiscard]] concurrency::task<std::wstring> CheckServerModeAsync(winrt::hstring serverUrl) override;
        [[nodiscard]] concurrency::task<SignInAttempt> SignInWithPasswordAsync(
            std::optional<winrt::hstring> serverUrl,
            winrt::hstring username,
            winrt::hstring password) override;
        [[nodiscard]] concurrency::task<SignInAttempt> RequestBrowserSignInAsync(
            std::function<void(winrt::hstring const& authUrl)> onOpened) override;
        void AcknowledgeSignIn() noexcept override;
        void CancelSignIn() noexcept override;
        [[nodiscard]] concurrency::task<winrt::hstring> LastServerAddressAsync() override;
        [[nodiscard]] concurrency::task<std::optional<std::chrono::milliseconds>>
            ProbeHealthAsync() override;
        [[nodiscard]] concurrency::task<void> SignOutAsync() override;
        [[nodiscard]] IdentityChangedToken AddIdentityChangedHandler(
            IdentityChangedHandler handler) override;
        void RemoveIdentityChangedHandler(IdentityChangedToken token) noexcept override;
        [[nodiscard]] IdentityChangedToken AddProfileChangedHandler(
            IdentityChangedHandler handler) override;
        void RemoveProfileChangedHandler(IdentityChangedToken token) noexcept override;

        // True only when /auth/me hydrated the current signed-in generation.
        // Token establishment alone is not a usable account session.
        [[nodiscard]] concurrency::task<bool> RefreshIdentityAsync();
        void HandleSessionRejected();
        // The composition root's reset of account-scoped services. It always runs
        // before the handlers added through AddIdentityChangedHandler, so those
        // find the services already reset.
        void SetIdentityChangedHandler(std::function<void()> handler);

    private:
        struct IdentityRefresh final
        {
            bool Loaded{};
            // Why it did not load, when it did not.
            std::wstring Error;
        };

        [[nodiscard]] concurrency::task<IdentityRefresh> RefreshIdentityDetailedAsync();
        // Ends a session a superseded sign-in established, on this PC only.
        [[nodiscard]] concurrency::task<void> DiscardAsync(std::uint64_t establishedGeneration);
        [[nodiscard]] std::stop_token BeginSignIn();
        void ClearIdentity();
        void NotifyIdentityChanged();
        void NotifyProfileChanged();

        std::shared_ptr<::HaloDesktop::Api::ApiClient> m_apiClient;
        std::shared_ptr<Auth::SessionController> m_controller;
        std::shared_ptr<Auth::SessionStore> m_store;
        std::shared_ptr<Auth::OidcSignInFlow> m_oidcSignInFlow;
        std::shared_ptr<Auth::SignInAddonRegistry const> m_addons;
        std::shared_ptr<DevicePreferencesStore> m_preferences;
        std::shared_ptr<NavigationService> m_navigation;
        // What the last discovery found to sign in with in the browser.
        std::optional<::HaloDesktop::Api::Dto::AuthConfig> m_browserConfig;
        std::shared_ptr<Auth::ISignInAddon> m_browserAddon;
        // Only the newest discovery's answer is kept.
        std::uint64_t m_discoveryVersion{};
        winrt::hstring m_userId;
        winrt::hstring m_userName;
        std::function<void()> m_identityChanged;
        std::unordered_map<IdentityChangedToken, IdentityChangedHandler> m_identityListeners;
        IdentityChangedToken m_nextIdentityToken{ 1 };
        std::unordered_map<IdentityChangedToken, IdentityChangedHandler> m_profileListeners;
        IdentityChangedToken m_nextProfileToken{ 1 };
        std::optional<winrt::hstring> m_plan;
        std::optional<winrt::hstring> m_avatarVersion;
        // Every sign-in takes the next number; CancelSignIn takes one too, so
        // an attempt that finds the number moved on discards what it got.
        std::atomic_uint64_t m_signInVersion{};
        // A session established and not yet taken by the sign-in screen.
        std::atomic_uint64_t m_pendingSessionGeneration{};
        std::mutex m_signInCancellationMutex;
        std::stop_source m_signInCancellation;
        bool m_isAdmin{};
    };
}
