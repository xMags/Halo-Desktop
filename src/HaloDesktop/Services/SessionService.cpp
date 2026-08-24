#include "pch.h"
#include "Services/SessionService.h"

#include "Api/ApiClient.h"
#include "Api/ApiError.h"
#include "Services/Auth/SessionController.h"
#include "Services/Auth/SessionStore.h"
#include "Services/Auth/OidcSignInFlow.h"
#include "Services/NavigationService.h"

#include <algorithm>
#include <cwctype>
#include <stdexcept>
#include <string>
#include <utility>
#include <winrt/Windows.Storage.h>

namespace
{
    constexpr wchar_t LegacyServerUrlKey[] = L"Session.ServerUrl";
    constexpr wchar_t LegacyUserNameKey[] = L"Session.UserName";
    constexpr wchar_t LegacySignedInKey[] = L"Session.IsSignedIn";

    winrt::hstring Trimmed(winrt::hstring const& input)
    {
        std::wstring value{ input };
        auto const first = std::find_if_not(value.begin(), value.end(), [](wchar_t character)
        {
            return std::iswspace(character) != 0;
        });
        auto const last = std::find_if_not(value.rbegin(), value.rend(), [](wchar_t character)
        {
            return std::iswspace(character) != 0;
        }).base();
        return first < last ? winrt::hstring{ std::wstring(first, last) } : winrt::hstring{};
    }

    void RemoveLegacyPrototypeSession()
    {
        auto const values = winrt::Windows::Storage::ApplicationData::Current().LocalSettings().Values();
        for (auto const key : { LegacyServerUrlKey, LegacyUserNameKey, LegacySignedInKey })
        {
            if (values.HasKey(key))
            {
                values.Remove(key);
            }
        }
    }
}

namespace HaloDesktop::Services
{
    SessionService::SessionService(
        std::shared_ptr<::HaloDesktop::Api::ApiClient> apiClient,
        std::shared_ptr<Auth::SessionController> controller,
        std::shared_ptr<Auth::SessionStore> store,
        std::shared_ptr<Auth::OidcSignInFlow> oidcSignInFlow,
        std::shared_ptr<NavigationService> navigation)
        : m_apiClient(std::move(apiClient)),
          m_controller(std::move(controller)),
          m_store(std::move(store)),
          m_oidcSignInFlow(std::move(oidcSignInFlow)),
          m_navigation(std::move(navigation))
    {
        if (!m_apiClient || !m_controller || !m_store || !m_oidcSignInFlow || !m_navigation)
        {
            throw std::invalid_argument{ "SessionService requires all dependencies." };
        }
        RemoveLegacyPrototypeSession();
    }

    winrt::hstring SessionService::ServerUrl() const
    {
        return m_apiClient->BaseUrl();
    }

    winrt::hstring SessionService::UserName() const
    {
        return m_userName;
    }

    winrt::hstring SessionService::UserId() const
    {
        return m_userId;
    }

    bool SessionService::IsSignedIn() const noexcept
    {
        return m_controller->IsSignedIn();
    }

    bool SessionService::IsAdmin() const noexcept
    {
        return m_isAdmin;
    }

    AuthenticationMode SessionService::Mode() const noexcept
    {
        return m_mode;
    }

    std::uint64_t SessionService::Generation() const noexcept
    {
        return m_controller->SessionGeneration();
    }

    concurrency::task<void> SessionService::RestoreAsync()
    {
        auto const uiContext = winrt::apartment_context{};
        co_await m_controller->RestoreAsync();
        auto identity = m_controller->IsSignedIn()
            ? co_await m_store->LoadIdentityAsync()
            : std::optional<Auth::StoredIdentity>{};
        co_await uiContext;
        ClearIdentity();
        if (identity)
        {
            m_userId = identity->UserId;
            m_userName = identity->Username;
            NotifyIdentityChanged();
        }
    }

    concurrency::task<AuthenticationMode> SessionService::DiscoverAuthenticationAsync()
    {
        auto const uiContext = winrt::apartment_context{};
        auto const config = co_await m_apiClient->GetAuthConfigAsync();
        co_await uiContext;
        m_mode = config.Mode == ::HaloDesktop::Api::Dto::AuthMode::Local
            ? AuthenticationMode::Local
            : AuthenticationMode::Oidc;
        m_oidcConfig = m_mode == AuthenticationMode::Oidc
            ? std::optional<::HaloDesktop::Api::Dto::AuthConfig>{ config }
            : std::nullopt;
        co_return m_mode;
    }

    concurrency::task<winrt::HaloDesktop::SignInOutcome> SessionService::SignInLocalAsync(
        winrt::hstring username,
        winrt::hstring password)
    {
        auto const uiContext = winrt::apartment_context{};
        auto const normalizedUserName = Trimmed(username);
        if (normalizedUserName.empty() || normalizedUserName.size() > 512
            || password.empty() || password.size() > 4096)
        {
            co_return winrt::HaloDesktop::SignInOutcome::InvalidCredentials;
        }

        try
        {
            ClearIdentity();
            co_await m_store->ClearIdentityAsync(m_controller->SessionGeneration() + 1);
            co_await m_controller->SignInLocalAsync(normalizedUserName, std::move(password));
        }
        catch (winrt::hresult_error const& error)
        {
            auto const status = ::HaloDesktop::Api::ApiError::HttpStatus(error.code());
            if (status && *status == 401)
            {
                co_return winrt::HaloDesktop::SignInOutcome::InvalidCredentials;
            }
            if (status && *status == 429)
            {
                co_return winrt::HaloDesktop::SignInOutcome::RateLimited;
            }
            co_return winrt::HaloDesktop::SignInOutcome::Unreachable;
        }
        catch (...)
        {
            co_return winrt::HaloDesktop::SignInOutcome::Unreachable;
        }

        co_await uiContext;
        m_userName = normalizedUserName;
        m_isAdmin = false;
        try
        {
            co_await RefreshIdentityAsync();
        }
        catch (...)
        {
        }
        co_return m_controller->IsSignedIn()
            ? winrt::HaloDesktop::SignInOutcome::Succeeded
            : winrt::HaloDesktop::SignInOutcome::Expired;
    }

    concurrency::task<winrt::HaloDesktop::SignInOutcome> SessionService::RequestBrowserSignInAsync()
    {
        auto const uiContext = winrt::apartment_context{};
        if (!m_oidcConfig)
        {
            co_return winrt::HaloDesktop::SignInOutcome::Unreachable;
        }
        auto result = co_await m_oidcSignInFlow->SignInAsync(*m_oidcConfig);
        if (result.Outcome != winrt::HaloDesktop::SignInOutcome::Succeeded || !result.Session)
        {
            co_return result.Outcome;
        }
        try
        {
            ClearIdentity();
            co_await m_store->ClearIdentityAsync(m_controller->SessionGeneration() + 1);
            co_await m_controller->SignInOidcAsync(std::move(*result.Session));
        }
        catch (...)
        {
            co_return winrt::HaloDesktop::SignInOutcome::Unreachable;
        }
        co_await uiContext;
        co_await RefreshIdentityAsync();
        co_return m_controller->IsSignedIn()
            ? winrt::HaloDesktop::SignInOutcome::Succeeded
            : winrt::HaloDesktop::SignInOutcome::Expired;
    }

    concurrency::task<void> SessionService::SignOutAsync()
    {
        auto const uiContext = winrt::apartment_context{};
        try { co_await m_controller->SignOutAsync(); } catch (...) {}
        try { co_await m_store->ClearIdentityAsync(m_controller->SessionGeneration()); } catch (...) {}
        co_await uiContext;
        ClearIdentity();
        m_navigation->ShowOverlay(Page::Login);
    }

    concurrency::task<void> SessionService::RefreshIdentityAsync()
    {
        auto const uiContext = winrt::apartment_context{};
        auto const generation = m_controller->SessionGeneration();
        ::HaloDesktop::Api::Dto::Me me;
        try
        {
            me = co_await m_apiClient->GetMeAsync();
        }
        catch (...)
        {
            co_return;
        }
        co_await uiContext;
        if (generation != m_controller->SessionGeneration() || !m_controller->IsSignedIn())
        {
            co_return;
        }
        m_userName = me.Username;
        m_userId = me.Id;
        m_isAdmin = me.IsAdmin;
        NotifyIdentityChanged();
        try
        {
            co_await m_store->SaveIdentityAsync(Auth::StoredIdentity{
                .UserId = me.Id,
                .Username = me.Username,
            }, generation);
        }
        catch (...)
        {
        }
        co_await uiContext;
    }

    void SessionService::HandleSessionRejected()
    {
        ClearIdentity();
        m_store->ClearIdentityAsync(m_controller->SessionGeneration()).then([](concurrency::task<void> task)
        {
            try { task.get(); } catch (...) {}
        });
        m_navigation->ShowOverlay(Page::Login);
    }

    void SessionService::SetIdentityChangedHandler(std::function<void()> handler)
    {
        m_identityChanged = std::move(handler);
        NotifyIdentityChanged();
    }

    void SessionService::ClearIdentity()
    {
        m_userName.clear();
        m_userId.clear();
        m_isAdmin = false;
        NotifyIdentityChanged();
    }

    void SessionService::NotifyIdentityChanged()
    {
        if (m_identityChanged)
        {
            m_identityChanged();
        }
    }
}
