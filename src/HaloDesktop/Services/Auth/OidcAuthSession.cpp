#include "pch.h"
#include "Services/Auth/OidcAuthSession.h"

#include "Api/BoundedHttpContent.h"
#include "Api/HttpExecutor.h"

#include <chrono>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>

namespace
{
    constexpr std::int64_t ExpiryMarginMilliseconds = 60'000;
    constexpr std::size_t MaximumTokenLength = 65536;

    std::int64_t NowMilliseconds()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    bool IsUnreserved(std::uint8_t byte)
    {
        return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z')
            || (byte >= '0' && byte <= '9') || byte == '-' || byte == '_'
            || byte == '.' || byte == '!' || byte == '~' || byte == '*'
            || byte == '\'' || byte == '(' || byte == ')';
    }

    winrt::hstring Encode(winrt::hstring const& input)
    {
        constexpr char hex[] = "0123456789ABCDEF";
        auto const utf8 = winrt::to_string(input);
        std::string result;
        for (auto const character : utf8)
        {
            auto const byte = static_cast<std::uint8_t>(character);
            if (IsUnreserved(byte))
            {
                result.push_back(static_cast<char>(byte));
            }
            else
            {
                result.push_back('%');
                result.push_back(hex[byte >> 4]);
                result.push_back(hex[byte & 15]);
            }
        }
        return winrt::to_hstring(result);
    }

    winrt::hstring Form(std::vector<std::pair<winrt::hstring, winrt::hstring>> const& values)
    {
        std::wstring body;
        for (auto const& [name, value] : values)
        {
            if (!body.empty())
            {
                body.push_back(L'&');
            }
            body.append(Encode(name));
            body.push_back(L'=');
            body.append(Encode(value));
        }
        return winrt::hstring{ body };
    }

    std::optional<winrt::hstring> OptionalString(
        winrt::Windows::Data::Json::JsonObject const& object,
        wchar_t const* name)
    {
        if (!object.HasKey(name))
        {
            return std::nullopt;
        }
        auto const value = object.GetNamedString(name);
        if (value.empty())
        {
            return std::nullopt;
        }
        if (value.size() > MaximumTokenLength)
        {
            throw std::invalid_argument{ "The identity provider returned an oversized token." };
        }
        return value;
    }

    ::HaloDesktop::Services::Auth::StoredOidcSession ApplyTokenResponse(
        ::HaloDesktop::Services::Auth::StoredOidcSession current,
        winrt::Windows::Data::Json::JsonObject const& object)
    {
        auto const accessToken = object.GetNamedString(L"access_token");
        if (accessToken.empty() || accessToken.size() > MaximumTokenLength)
        {
            throw std::invalid_argument{ "The identity provider returned no access token." };
        }
        current.AccessToken = accessToken;
        if (auto const refresh = OptionalString(object, L"refresh_token"))
        {
            current.RefreshToken = refresh;
        }
        if (auto const idToken = OptionalString(object, L"id_token"))
        {
            current.IdToken = idToken;
        }
        current.ExpiresAt = 0;
        if (object.HasKey(L"expires_in"))
        {
            auto const seconds = object.GetNamedNumber(L"expires_in");
            if (!std::isfinite(seconds) || seconds <= 0)
            {
                throw std::invalid_argument{ "The identity provider returned an invalid token lifetime." };
            }
            current.ExpiresAt = NowMilliseconds() + static_cast<std::int64_t>(seconds * 1000.0);
        }
        return current;
    }
}

namespace HaloDesktop::Services::Auth
{
    OidcAuthSession::OidcAuthSession(
        std::shared_ptr<::HaloDesktop::Api::HttpExecutor> executor,
        std::shared_ptr<SessionStore> store)
        : m_executor(std::move(executor)),
          m_store(std::move(store))
    {
        if (!m_executor || !m_store)
        {
            throw std::invalid_argument{ "OidcAuthSession requires HTTP and storage dependencies." };
        }
    }

    void OidcAuthSession::Restore(StoredOidcSession session)
    {
        std::scoped_lock const lock{ m_mutex };
        m_session = std::move(session);
        ++m_revision;
    }

    concurrency::task<void> OidcAuthSession::EstablishAsync(StoredOidcSession session)
    {
        co_await winrt::resume_background();
        std::uint64_t revision{};
        {
            std::scoped_lock const lock{ m_mutex };
            m_session = session;
            revision = ++m_revision;
        }
        co_await PersistUntilCurrentAsync(revision, std::move(session));
    }

    concurrency::task<std::optional<winrt::hstring>> OidcAuthSession::AccessTokenAsync()
    {
        co_await winrt::resume_background();
        StoredOidcSession current;
        {
            std::scoped_lock const lock{ m_mutex };
            if (!m_session)
            {
                co_return std::nullopt;
            }
            current = *m_session;
        }
        if (NowMilliseconds() < current.ExpiresAt - ExpiryMarginMilliseconds)
        {
            co_return current.AccessToken;
        }
        co_return co_await RefreshAccessTokenAsync();
    }

    concurrency::task<std::optional<winrt::hstring>> OidcAuthSession::RefreshAccessTokenAsync()
    {
        co_await winrt::resume_background();
        std::shared_ptr<concurrency::task<std::optional<winrt::hstring>>> task;
        std::uint64_t flightId{};
        {
            std::scoped_lock const lock{ m_mutex };
            if (!m_session || !m_session->RefreshToken)
            {
                co_return std::nullopt;
            }
            if (m_refreshFlight)
            {
                task = m_refreshFlight->Task;
                flightId = m_refreshFlight->Id;
            }
            else
            {
                flightId = ++m_nextFlightId;
                task = std::make_shared<concurrency::task<std::optional<winrt::hstring>>>(
                    RefreshNowAsync(*m_session, m_revision));
                m_refreshFlight = RefreshFlight{ flightId, task };
            }
        }

        try
        {
            auto result = co_await *task;
            ClearFlight(flightId);
            co_return result;
        }
        catch (...)
        {
            ClearFlight(flightId);
            throw;
        }
    }

    concurrency::task<void> OidcAuthSession::SignOutAsync(bool endIdentityProviderSession)
    {
        co_await winrt::resume_background();
        std::optional<StoredOidcSession> current;
        std::uint64_t revision{};
        {
            std::scoped_lock const lock{ m_mutex };
            current = m_session;
            m_session.reset();
            revision = ++m_revision;
        }
        co_await PersistUntilCurrentAsync(revision, std::nullopt);
        if (!current)
        {
            co_return;
        }

        if (current->RefreshToken && current->RevocationEndpoint)
        {
            try
            {
                static_cast<void>(co_await m_executor->SendFormWithoutRedirectAsync(
                    winrt::Windows::Foundation::Uri{ *current->RevocationEndpoint },
                    Form({
                        { L"client_id", current->ClientId },
                        { L"token", *current->RefreshToken },
                        { L"token_type_hint", L"refresh_token" },
                    })));
            }
            catch (...)
            {
            }
        }

        if (endIdentityProviderSession && current->EndSessionEndpoint)
        {
            auto url = *current->EndSessionEndpoint;
            if (current->IdToken)
            {
                auto const separator = std::wstring{ url }.find(L'?') == std::wstring::npos ? L"?" : L"&";
                url = url + separator + L"id_token_hint=" + Encode(*current->IdToken);
            }
            try
            {
                static_cast<void>(co_await winrt::Windows::System::Launcher::LaunchUriAsync(
                    winrt::Windows::Foundation::Uri{ url }));
            }
            catch (...)
            {
            }
        }
    }

    concurrency::task<bool> OidcAuthSession::ClearIfRevisionAsync(std::uint64_t expectedRevision)
    {
        co_await winrt::resume_background();
        std::uint64_t revision{};
        {
            std::scoped_lock const lock{ m_mutex };
            if (m_revision != expectedRevision)
            {
                co_return false;
            }
            m_session.reset();
            revision = ++m_revision;
        }
        co_await PersistUntilCurrentAsync(revision, std::nullopt);
        co_return true;
    }

    std::uint64_t OidcAuthSession::Revision() const noexcept
    {
        std::scoped_lock const lock{ m_mutex };
        return m_revision;
    }

    concurrency::task<std::optional<winrt::hstring>> OidcAuthSession::RefreshNowAsync(
        StoredOidcSession observed,
        std::uint64_t observedRevision)
    {
        co_await winrt::resume_background();
        auto const response = co_await m_executor->SendFormWithoutRedirectAsync(
            winrt::Windows::Foundation::Uri{ observed.TokenEndpoint },
            Form({
                { L"grant_type", L"refresh_token" },
                { L"client_id", observed.ClientId },
                { L"refresh_token", *observed.RefreshToken },
            }));
        auto const body = co_await Api::ReadBoundedJsonTextAsync(response.Content());

        winrt::Windows::Data::Json::JsonObject object;
        try
        {
            object = winrt::Windows::Data::Json::JsonObject::Parse(body);
        }
        catch (...)
        {
            throw winrt::hresult_invalid_argument{ L"The identity provider returned invalid JSON." };
        }

        auto const oauthError = OptionalString(object, L"error");
        if (oauthError && *oauthError == L"invalid_grant")
        {
            static_cast<void>(co_await ClearIfRevisionAsync(observedRevision));
            co_return std::nullopt;
        }
        if (!response.IsSuccessStatusCode() || oauthError)
        {
            throw winrt::hresult_error{ E_FAIL, L"The identity provider rejected token refresh." };
        }

        auto next = ApplyTokenResponse(observed, object);
        std::uint64_t revision{};
        {
            std::scoped_lock const lock{ m_mutex };
            if (m_revision != observedRevision || !m_session
                || m_session->AccessToken != observed.AccessToken)
            {
                co_return m_session
                    ? std::optional<winrt::hstring>{ m_session->AccessToken }
                    : std::nullopt;
            }
            m_session = next;
            revision = ++m_revision;
        }
        co_await PersistUntilCurrentAsync(revision, next);
        co_return next.AccessToken;
    }

    concurrency::task<void> OidcAuthSession::PersistUntilCurrentAsync(
        std::uint64_t revision,
        std::optional<StoredOidcSession> session)
    {
        co_await winrt::resume_background();
        for (;;)
        {
            if (session)
            {
                co_await m_store->SaveAsync(StoredSession{
                    .Kind = StoredSessionKind::Oidc,
                    .Oidc = session,
                });
            }
            else
            {
                co_await m_store->ClearAsync();
            }

            std::scoped_lock const lock{ m_mutex };
            if (revision == m_revision)
            {
                co_return;
            }
            revision = m_revision;
            session = m_session;
        }
    }

    void OidcAuthSession::ClearFlight(std::uint64_t flightId) noexcept
    {
        std::scoped_lock const lock{ m_mutex };
        if (m_refreshFlight && m_refreshFlight->Id == flightId)
        {
            m_refreshFlight.reset();
        }
    }
}
