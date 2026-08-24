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

    winrt::hstring NormalizeBaseUrl(winrt::hstring const& input)
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
}

namespace HaloDesktop::Services::Auth
{
    LocalAuthSession::LocalAuthSession(
        winrt::hstring baseUrl,
        std::shared_ptr<::HaloDesktop::Api::HttpExecutor> executor,
        std::shared_ptr<SessionStore> store)
        : m_baseUrl(NormalizeBaseUrl(baseUrl)),
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
        std::scoped_lock const lock{ m_mutex };
        m_session = std::move(session);
        ++m_revision;
    }

    concurrency::task<::HaloDesktop::Api::Dto::IssuedToken> LocalAuthSession::LoginAsync(
        winrt::hstring username,
        winrt::hstring password)
    {
        co_await winrt::resume_background();

        winrt::Windows::Data::Json::JsonObject request;
        request.Insert(L"username", winrt::Windows::Data::Json::JsonValue::CreateStringValue(username));
        request.Insert(L"password", winrt::Windows::Data::Json::JsonValue::CreateStringValue(password));
        auto const response = co_await m_executor->SendJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Post(),
            Endpoint(L"/auth/login"),
            request.Stringify());
        auto const issued = ::HaloDesktop::Api::Mappers::ParseIssuedToken(response);

        std::uint64_t establishedRevision{};
        {
            std::scoped_lock const lock{ m_mutex };
            m_session = StoredLocalSession{
                .Token = issued.Token,
                .ExpiresAt = issued.ExpiresAt,
            };
            establishedRevision = ++m_revision;
        }
        co_await PersistUntilCurrentAsync(establishedRevision, StoredLocalSession{
            .Token = issued.Token,
            .ExpiresAt = issued.ExpiresAt,
        });

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

        try
        {
            co_return co_await RefreshAccessTokenAsync();
        }
        catch (winrt::hresult_error const& error)
        {
            if (!::HaloDesktop::Api::ApiError::IsTransport(error.code()))
            {
                throw;
            }
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
        std::uint64_t revision{};
        {
            std::scoped_lock const lock{ m_mutex };
            m_session.reset();
            revision = ++m_revision;
        }
        co_await PersistUntilCurrentAsync(revision, std::nullopt);
    }

    concurrency::task<bool> LocalAuthSession::ClearIfRevisionAsync(std::uint64_t expectedRevision)
    {
        co_await winrt::resume_background();
        std::uint64_t clearedRevision{};
        {
            std::scoped_lock const lock{ m_mutex };
            if (m_revision != expectedRevision)
            {
                co_return false;
            }
            m_session.reset();
            clearedRevision = ++m_revision;
        }
        co_await PersistUntilCurrentAsync(clearedRevision, std::nullopt);
        co_return true;
    }

    std::uint64_t LocalAuthSession::Revision() const noexcept
    {
        std::scoped_lock const lock{ m_mutex };
        return m_revision;
    }

    concurrency::task<std::optional<winrt::hstring>> LocalAuthSession::RefreshNowAsync(
        StoredLocalSession observed,
        std::uint64_t observedRevision)
    {
        co_await winrt::resume_background();

        winrt::Windows::Data::Json::JsonObject emptyBody;
        winrt::Windows::Data::Json::IJsonValue response{ nullptr };
        bool rejected{};
        try
        {
            response = co_await m_executor->SendJsonAsync(
                winrt::Windows::Web::Http::HttpMethod::Post(),
                Endpoint(L"/auth/refresh"),
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
            static_cast<void>(co_await ClearIfRevisionAsync(observedRevision));
            co_return std::nullopt;
        }

        auto const issued = ::HaloDesktop::Api::Mappers::ParseIssuedToken(response);
        std::uint64_t nextRevision{};
        {
            std::scoped_lock const lock{ m_mutex };
            if (m_revision != observedRevision || !m_session || m_session->Token != observed.Token)
            {
                co_return m_session
                    ? std::optional<winrt::hstring>{ m_session->Token }
                    : std::nullopt;
            }
            m_session = StoredLocalSession{
                .Token = issued.Token,
                .ExpiresAt = issued.ExpiresAt,
            };
            nextRevision = ++m_revision;
        }
        co_await PersistUntilCurrentAsync(nextRevision, StoredLocalSession{
            .Token = issued.Token,
            .ExpiresAt = issued.ExpiresAt,
        });
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

    winrt::Windows::Foundation::Uri LocalAuthSession::Endpoint(wchar_t const* path) const
    {
        return winrt::Windows::Foundation::Uri{ m_baseUrl + path };
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
