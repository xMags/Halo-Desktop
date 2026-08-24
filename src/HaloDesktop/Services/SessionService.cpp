#include "pch.h"
#include "Services/SessionService.h"

#include "Api/ApiClient.h"
#include "Api/ApiError.h"
#include "Services/Auth/SessionController.h"
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
        std::shared_ptr<NavigationService> navigation)
        : m_apiClient(std::move(apiClient)),
          m_controller(std::move(controller)),
          m_navigation(std::move(navigation))
    {
        if (!m_apiClient || !m_controller || !m_navigation)
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
        co_await uiContext;
        ClearIdentity();
    }

    concurrency::task<AuthenticationMode> SessionService::DiscoverAuthenticationAsync()
    {
        auto const uiContext = winrt::apartment_context{};
        auto const config = co_await m_apiClient->GetAuthConfigAsync();
        co_await uiContext;
        m_mode = config.Mode == ::HaloDesktop::Api::Dto::AuthMode::Local
            ? AuthenticationMode::Local
            : AuthenticationMode::Oidc;
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
        co_return winrt::HaloDesktop::SignInOutcome::Unreachable;
    }

    concurrency::task<void> SessionService::SignOutAsync()
    {
        auto const uiContext = winrt::apartment_context{};
        co_await m_controller->SignOutAsync();
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
        m_isAdmin = me.IsAdmin;
    }

    void SessionService::HandleSessionRejected()
    {
        ClearIdentity();
        m_navigation->ShowOverlay(Page::Login);
    }

    void SessionService::ClearIdentity()
    {
        m_userName.clear();
        m_isAdmin = false;
    }
}
