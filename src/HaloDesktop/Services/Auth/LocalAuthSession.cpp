#include "pch.h"
#include "Services/Auth/LocalAuthSession.h"

#include "Api/ApiError.h"
#include "Api/HttpExecutor.h"

#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Web.Http.h>

namespace
{
    constexpr std::int64_t ExpiryMarginMilliseconds = 60'000;
    constexpr std::int64_t ProactiveRefreshMilliseconds = 15LL * 24 * 60 * 60 * 1000;

    std::int64_t NowMilliseconds()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    winrt::hstring NormalizeServerUrl(winrt::hstring const& input)
    {
        std::wstring value{ input };
        while (!value.empty() && value.back() == L'/')
        {
            value.pop_back();
        }
        if (value.empty())
        {
            throw std::invalid_argument{ "A server base URL is required." };
        }
        return winrt::hstring{ value };
    }

    winrt::Windows::Foundation::Uri Endpoint(winrt::hstring const& serverUrl, wchar_t const* path)
    {
        return winrt::Windows::Foundation::Uri{ serverUrl + path };
    }
}

namespace HaloDesktop::Services::Auth
{
    LocalAuthSession::LocalAuthSession(
        winrt::hstring builtInServerUrl,
        std::shared_ptr<::HaloDesktop::Api::HttpExecutor> executor,
        std::shared_ptr<SessionStore> store)
        : m_builtInServerUrl(NormalizeServerUrl(builtInServerUrl)),
          m_executor(std::move(executor)),
          m_store(std::move(store))
    {
        if (!m_executor || !m_store)
        {
            throw std::invalid_argument{ "LocalAuthSession requires HTTP and storage dependencies." };
        }
    }

    void LocalAuthSession::Restore(StoredLocalSession session)
    {
        if (!session.ServerUrl || session.ServerUrl->empty())
        {
            session.ServerUrl = m_builtInServerUrl;
        }
        std::scoped_lock const lock{ m_mutex };
        m_session = std::move(session);
        m_refreshFlight.reset();
        ++m_revision;
    }

    concurrency::task<::HaloDesktop::Api::Dto::IssuedToken> LocalAuthSession::LoginAsync(
        winrt::hstring serverUrl,
        winrt::hstring username,
        winrt::hstring password)
    {
        co_await winrt::resume_background();
        auto const server = NormalizeServerUrl(serverUrl);

        winrt::Windows::Data::Json::IJsonValue response{ nullptr };
        {
            winrt::Windows::Data::Json::JsonObject request;
            request.Insert(L"username", winrt::Windows::Data::Json::JsonValue::CreateStringValue(username));
            request.Insert(L"password", winrt::Windows::Data::Json::JsonValue::CreateStringValue(password));
            password = winrt::hstring{};
            // Never after a redirect: the credentials go to the exact origin
            // the session is for, or nowhere.
            response = co_await m_executor->SendJsonWithoutRedirectAsync(
                winrt::Windows::Web::Http::HttpMethod::Post(),
                Endpoint(server, L"/auth/login"),
                request.Stringify());
        }
        auto const issued = ::HaloDesktop::Api::Mappers::ParseIssuedToken(response);
        StoredLocalSession established{
            .Token = issued.Token,
            .ExpiresAt = issued.ExpiresAt,
            .ServerUrl = server,
        };

        std::uint64_t establishedRevision{};
        {
            std::scoped_lock const lock{ m_mutex };
            m_session = established;
            // A refresh still in flight belongs to the session just replaced.
            m_refreshFlight.reset();
            establishedRevision = ++m_revision;
        }
        co_await PersistUntilCurrentAsync(establishedRevision, established);

        {
            std::scoped_lock const lock{ m_mutex };
            if (m_revision != establishedRevision || !m_session || m_session->Token != issued.Token)
            {
                throw winrt::hresult_canceled{};
            }
        }
        co_return issued;
    }

    concurrency::task<std::optional<winrt::hstring>> LocalAuthSession::AccessTokenAsync()
    {
        co_await winrt::resume_background();

        StoredLocalSession current;
        {
            std::scoped_lock const lock{ m_mutex };
            if (!m_session)
            {
                co_return std::nullopt;
            }
            current = *m_session;
        }

        auto const remaining = current.ExpiresAt - NowMilliseconds();
        if (remaining > ProactiveRefreshMilliseconds)
        {
            co_return current.Token;
        }
        if (remaining <= ExpiryMarginMilliseconds)
        {
            co_return co_await RefreshAccessTokenAsync();
        }

        // Inside the proactive window the current token is still valid, so a
        // refresh that fails for any reason leaves it in use. A refusal (null)
        // has ended the session, which leaves no token to fall back on.
        try
        {
            co_return co_await RefreshAccessTokenAsync();
        }
        catch (...)
        {
            // Falls through to the token still in hand.
        }

        std::scoped_lock const lock{ m_mutex };
        co_return m_session ? std::optional<winrt::hstring>{ m_session->Token } : std::nullopt;
    }

    concurrency::task<std::optional<winrt::hstring>> LocalAuthSession::RefreshAccessTokenAsync()
    {
        co_await winrt::resume_background();

        std::shared_ptr<concurrency::task<std::optional<winrt::hstring>>> task;
        std::uint64_t flightId{};
        {
            std::scoped_lock const lock{ m_mutex };
            if (!m_session)
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

    concurrency::task<void> LocalAuthSession::ClearAsync()
    {
        co_await winrt::resume_background();
        std::optional<StoredLocalSession> previous;
        std::uint64_t revision{};
        {
            std::scoped_lock const lock{ m_mutex };
            previous = m_session;
            m_session.reset();
            // A refresh still in flight belongs to the session that just ended.
            m_refreshFlight.reset();
            revision = ++m_revision;
        }
        try
        {
            co_await PersistUntilCurrentAsync(revision, std::nullopt);
        }
        catch (...)
        {
            std::scoped_lock const lock{ m_mutex };
            if (m_revision == revision)
            {
                m_session = std::move(previous);
                ++m_revision;
            }
            throw;
        }
    }

    concurrency::task<bool> LocalAuthSession::ClearIfRevisionAsync(std::uint64_t expectedRevision)
    {
        co_await winrt::resume_background();
        std::optional<StoredLocalSession> previous;
        std::uint64_t clearedRevision{};
        {
            std::scoped_lock const lock{ m_mutex };
            if (m_revision != expectedRevision)
            {
                co_return false;
            }
            previous = m_session;
            m_session.reset();
            m_refreshFlight.reset();
            clearedRevision = ++m_revision;
        }
        try
        {
            co_await PersistUntilCurrentAsync(clearedRevision, std::nullopt);
        }
        catch (...)
        {
            std::scoped_lock const lock{ m_mutex };
            if (m_revision == clearedRevision)
            {
                m_session = std::move(previous);
                ++m_revision;
            }
            throw;
        }
        co_return true;
    }

    std::uint64_t LocalAuthSession::Revision() const noexcept
    {
        std::scoped_lock const lock{ m_mutex };
        return m_revision;
    }

    std::optional<winrt::hstring> LocalAuthSession::ServerUrl() const
    {
        std::scoped_lock const lock{ m_mutex };
        return m_session ? m_session->ServerUrl : std::nullopt;
    }

    concurrency::task<std::optional<winrt::hstring>> LocalAuthSession::RefreshNowAsync(
        StoredLocalSession observed,
        std::uint64_t observedRevision)
    {
        co_await winrt::resume_background();
        auto const server = observed.ServerUrl ? *observed.ServerUrl : m_builtInServerUrl;

        winrt::Windows::Data::Json::JsonObject emptyBody;
        winrt::Windows::Data::Json::IJsonValue response{ nullptr };
        bool rejected{};
        try
        {
            response = co_await m_executor->SendJsonWithoutRedirectAsync(
                winrt::Windows::Web::Http::HttpMethod::Post(),
                Endpoint(server, L"/auth/refresh"),
                emptyBody.Stringify(),
                { { L"Authorization", winrt::hstring{ L"Bearer " } + observed.Token } });
        }
        catch (winrt::hresult_error const& error)
        {
            auto const status = ::HaloDesktop::Api::ApiError::HttpStatus(error.code());
            if (!status || *status != 401)
            {
                throw;
            }
            rejected = true;
        }
        if (rejected)
        {
            // A 401 is the local-mode invalid_grant, but only for the session it
            // was asked about: after a sign-out, or a sign-in as someone else, a
            // late answer must neither bring the old session back nor end the
            // new one.
            if (co_await ClearIfRevisionAsync(observedRevision))
            {
                co_return std::nullopt;
            }
            std::scoped_lock const lock{ m_mutex };
            co_return m_session ? std::optional<winrt::hstring>{ m_session->Token } : std::nullopt;
        }

        auto const issued = ::HaloDesktop::Api::Mappers::ParseIssuedToken(response);
        StoredLocalSession next{
            .Token = issued.Token,
            .ExpiresAt = issued.ExpiresAt,
            .ServerUrl = server,
        };
        std::uint64_t nextRevision{};
        {
            std::scoped_lock const lock{ m_mutex };
            if (m_revision != observedRevision || !m_session || m_session->Token != observed.Token
                || m_session->ServerUrl != observed.ServerUrl)
            {
                co_return m_session
                    ? std::optional<winrt::hstring>{ m_session->Token }
                    : std::nullopt;
            }
            m_session = next;
            nextRevision = ++m_revision;
        }
        co_await PersistUntilCurrentAsync(nextRevision, next);
        co_return issued.Token;
    }

    concurrency::task<void> LocalAuthSession::PersistUntilCurrentAsync(
        std::uint64_t revision,
        std::optional<StoredLocalSession> session)
    {
        co_await winrt::resume_background();

        for (;;)
        {
            if (session)
            {
                co_await m_store->SaveAsync(StoredSession{
                    .Kind = StoredSessionKind::Local,
                    .Local = session,
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

    void LocalAuthSession::ClearFlight(std::uint64_t flightId) noexcept
    {
        std::scoped_lock const lock{ m_mutex };
        if (m_refreshFlight && m_refreshFlight->Id == flightId)
        {
            m_refreshFlight.reset();
        }
    }
}
