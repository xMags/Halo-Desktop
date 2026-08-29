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
#include <winrt/Windows.System.Threading.h>

namespace
{
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

        std::uint64_t establishedGeneration{};
        try
        {
            ClearIdentity();
            co_await m_store->ClearIdentityAsync(m_controller->SessionGeneration() + 1);
            establishedGeneration = co_await m_controller->SignInLocalAsync(
                normalizedUserName,
                std::move(password));
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
        if (!co_await RefreshIdentityAsync())
        {
            try
            {
                co_await m_controller->RejectSessionAsync(establishedGeneration);
            }
            catch (...)
            {
            }
            co_return winrt::HaloDesktop::SignInOutcome::Unreachable;
        }
        co_await uiContext;
        co_return m_controller->IsSignedIn() && !m_userId.empty()
            ? winrt::HaloDesktop::SignInOutcome::Succeeded
            : winrt::HaloDesktop::SignInOutcome::Expired;
    }

    concurrency::task<winrt::HaloDesktop::SignInOutcome> SessionService::RequestBrowserSignInAsync()
    {
        auto const uiContext = winrt::apartment_context{};
        auto const version = ++m_browserSignInVersion;
        if (!m_oidcConfig)
        {
            co_return winrt::HaloDesktop::SignInOutcome::Unreachable;
        }
        auto result = co_await m_oidcSignInFlow->SignInAsync(*m_oidcConfig);
        co_await uiContext;
        if (version != m_browserSignInVersion)
        {
            co_return winrt::HaloDesktop::SignInOutcome::Declined;
        }
        if (result.Outcome != winrt::HaloDesktop::SignInOutcome::Succeeded || !result.Session)
        {
            co_return result.Outcome;
        }
        std::uint64_t establishedGeneration{};
        try
        {
            ClearIdentity();
            co_await m_store->ClearIdentityAsync(m_controller->SessionGeneration() + 1);
            co_await uiContext;
            if (version != m_browserSignInVersion)
            {
                co_return winrt::HaloDesktop::SignInOutcome::Declined;
            }
            establishedGeneration = co_await m_controller->SignInOidcAsync(std::move(*result.Session));
            m_pendingBrowserSessionGeneration.store(establishedGeneration);
        }
        catch (...)
        {
            co_return winrt::HaloDesktop::SignInOutcome::Unreachable;
        }
        co_await uiContext;
        if (version != m_browserSignInVersion)
        {
            auto expectedGeneration = establishedGeneration;
            static_cast<void>(m_pendingBrowserSessionGeneration.compare_exchange_strong(
                expectedGeneration, 0));
            co_await m_controller->RejectSessionAsync(establishedGeneration);
            co_return winrt::HaloDesktop::SignInOutcome::Declined;
        }
        auto const identityLoaded = co_await RefreshIdentityAsync();
        co_await uiContext;
        if (version != m_browserSignInVersion)
        {
            auto expectedGeneration = establishedGeneration;
            static_cast<void>(m_pendingBrowserSessionGeneration.compare_exchange_strong(
                expectedGeneration, 0));
            co_await m_controller->RejectSessionAsync(establishedGeneration);
            co_return winrt::HaloDesktop::SignInOutcome::Declined;
        }
        if (identityLoaded && m_controller->IsSignedIn() && !m_userId.empty())
        {
            co_return winrt::HaloDesktop::SignInOutcome::Succeeded;
        }
        auto expectedGeneration = establishedGeneration;
        static_cast<void>(m_pendingBrowserSessionGeneration.compare_exchange_strong(
            expectedGeneration, 0));
        try
        {
            co_await m_controller->RejectSessionAsync(establishedGeneration);
        }
        catch (...)
        {
        }
        co_return identityLoaded
            ? winrt::HaloDesktop::SignInOutcome::Expired
            : winrt::HaloDesktop::SignInOutcome::Unreachable;
    }

    void SessionService::AcknowledgeBrowserSignIn() noexcept
    {
        m_pendingBrowserSessionGeneration.store(0);
    }

    void SessionService::CancelBrowserSignIn() noexcept
    {
        ++m_browserSignInVersion;
        m_oidcSignInFlow->Cancel();
        auto const establishedGeneration = m_pendingBrowserSessionGeneration.exchange(0);
        if (establishedGeneration != 0)
        {
            m_controller->RejectSessionAsync(establishedGeneration).then([](concurrency::task<void> rejected)
            {
                try { rejected.get(); } catch (...) {}
            });
        }
    }

    concurrency::task<std::optional<std::chrono::milliseconds>> SessionService::ProbeHealthAsync()
    {
        concurrency::task_completion_event<std::optional<std::chrono::milliseconds>> completion;
        m_apiClient->GetHealthAsync().then([completion](concurrency::task<::HaloDesktop::Api::Dto::HealthStatus> task) mutable
        {
            try
            {
                auto const health = task.get();
                completion.set(health.Ok
                    ? std::optional<std::chrono::milliseconds>{ health.RoundTrip }
                    : std::nullopt);
            }
            catch (...)
            {
                completion.set(std::nullopt);
            }
        });
        auto const timeout = winrt::Windows::System::Threading::ThreadPoolTimer::CreateTimer(
            [completion](auto const&) mutable { completion.set(std::nullopt); },
            std::chrono::seconds{ 6 });
        auto result = co_await concurrency::create_task(completion);
        timeout.Cancel();
        co_return result;
    }

    concurrency::task<void> SessionService::SignOutAsync()
    {
        auto const uiContext = winrt::apartment_context{};
        co_await m_controller->SignOutAsync();
        try { co_await m_store->ClearIdentityAsync(m_controller->SessionGeneration()); } catch (...) {}
        co_await uiContext;
        ClearIdentity();
        m_navigation->ShowOverlay(Page::Login);
    }

    concurrency::task<bool> SessionService::RefreshIdentityAsync()
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
            co_return false;
        }
        co_await uiContext;
        if (generation != m_controller->SessionGeneration() || !m_controller->IsSignedIn())
        {
            co_return false;
        }
        auto const accountChanged = m_userId != me.Id;
        m_userName = me.Username;
        m_userId = me.Id;
        m_isAdmin = me.IsAdmin;
        if (accountChanged)
        {
            NotifyIdentityChanged();
        }
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
        co_return generation == m_controller->SessionGeneration()
            && m_controller->IsSignedIn()
            && !m_userId.empty();
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
        auto const accountChanged = !m_userId.empty();
        m_userName.clear();
        m_userId.clear();
        m_isAdmin = false;
        if (accountChanged)
        {
            NotifyIdentityChanged();
        }
    }

    void SessionService::NotifyIdentityChanged()
    {
        if (m_identityChanged)
        {
            m_identityChanged();
        }
    }
}
