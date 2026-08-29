#include "pch.h"
#include "Services/Auth/SessionController.h"

#include "Services/Auth/LocalAuthSession.h"
#include "Services/Auth/OidcAuthSession.h"
#include "Services/Auth/SessionStore.h"
#include "Services/QueryCache.h"

#include <stdexcept>
#include <utility>

namespace HaloDesktop::Services::Auth
{
    SessionController::SessionController(
        std::shared_ptr<SessionStore> store,
        std::shared_ptr<LocalAuthSession> localSession,
        std::shared_ptr<OidcAuthSession> oidcSession,
        std::shared_ptr<::HaloDesktop::Services::QueryCache> queryCache,
        winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher)
        : m_store(std::move(store)),
          m_localSession(std::move(localSession)),
          m_oidcSession(std::move(oidcSession)),
          m_queryCache(std::move(queryCache)),
          m_dispatcher(std::move(dispatcher))
    {
        if (!m_store || !m_localSession || !m_oidcSession || !m_queryCache || !m_dispatcher)
        {
            throw std::invalid_argument{ "SessionController requires all dependencies." };
        }
    }

    concurrency::task<void> SessionController::RestoreAsync()
    {
        co_await winrt::resume_background();
        auto const stored = co_await m_store->LoadAsync();
        if (!stored)
        {
            co_return;
        }
        if (stored->Kind == StoredSessionKind::Local && stored->Local)
        {
            m_localSession->Restore(*stored->Local);
            std::scoped_lock const lock{ m_mutex };
            m_kind = SessionKind::Local;
            ++m_generation;
        }
        else if (stored->Kind == StoredSessionKind::Oidc && stored->Oidc)
        {
            m_oidcSession->Restore(*stored->Oidc);
            std::scoped_lock const lock{ m_mutex };
            m_kind = SessionKind::Oidc;
            ++m_generation;
        }
    }

    concurrency::task<void> SessionController::SignInLocalAsync(
        winrt::hstring username,
        winrt::hstring password)
    {
        co_await winrt::resume_background();
        co_await m_oidcSession->SignOutAsync(false);
        static_cast<void>(co_await m_localSession->LoginAsync(
            std::move(username),
            std::move(password)));
        std::scoped_lock const lock{ m_mutex };
        m_kind = SessionKind::Local;
        ++m_generation;
    }

    concurrency::task<std::uint64_t> SessionController::SignInOidcAsync(StoredOidcSession session)
    {
        co_await winrt::resume_background();
        co_await m_localSession->ClearAsync();
        co_await m_oidcSession->EstablishAsync(std::move(session));
        std::scoped_lock const lock{ m_mutex };
        m_kind = SessionKind::Oidc;
        co_return ++m_generation;
    }

    concurrency::task<void> SessionController::SignOutAsync()
    {
        co_await winrt::resume_background();
        SessionKind kind{};
        std::uint64_t generation{};
        {
            std::scoped_lock const lock{ m_mutex };
            kind = m_kind;
            generation = m_generation;
        }
        if (kind == SessionKind::Local)
        {
            co_await m_localSession->ClearAsync();
        }
        else if (kind == SessionKind::Oidc)
        {
            co_await m_oidcSession->SignOutAsync(true);
        }
        co_await wil::resume_foreground(m_dispatcher);
        {
            std::scoped_lock const lock{ m_mutex };
            if (m_kind != kind || m_generation != generation)
            {
                co_return;
            }
            m_kind = SessionKind::None;
            ++m_generation;
        }
        m_queryCache->Clear();
    }

    bool SessionController::IsSignedIn() const noexcept
    {
        std::scoped_lock const lock{ m_mutex };
        return m_kind != SessionKind::None;
    }

    SessionKind SessionController::Kind() const noexcept
    {
        std::scoped_lock const lock{ m_mutex };
        return m_kind;
    }

    void SessionController::SetRejectedHandler(RejectedHandler handler)
    {
        std::scoped_lock const lock{ m_mutex };
        m_rejectedHandler = std::move(handler);
    }

    concurrency::task<std::optional<winrt::hstring>> SessionController::AccessTokenAsync()
    {
        co_await winrt::resume_background();
        auto const kind = Kind();
        if (kind == SessionKind::None)
        {
            co_return std::nullopt;
        }
        if (kind == SessionKind::Local)
        {
            co_return co_await m_localSession->AccessTokenAsync();
        }
        co_return co_await m_oidcSession->AccessTokenAsync();
    }

    concurrency::task<std::optional<winrt::hstring>> SessionController::RefreshAccessTokenAsync()
    {
        co_await winrt::resume_background();
        auto const kind = Kind();
        if (kind == SessionKind::None)
        {
            co_return std::nullopt;
        }
        if (kind == SessionKind::Local)
        {
            co_return co_await m_localSession->RefreshAccessTokenAsync();
        }
        co_return co_await m_oidcSession->RefreshAccessTokenAsync();
    }

    concurrency::task<void> SessionController::RejectSessionAsync(std::uint64_t expectedGeneration)
    {
        co_await winrt::resume_background();

        std::uint64_t sessionRevision{};
        SessionKind kind{ SessionKind::None };
        {
            std::scoped_lock const lock{ m_mutex };
            if (m_generation != expectedGeneration || m_kind == SessionKind::None)
            {
                co_return;
            }
            kind = m_kind;
            sessionRevision = kind == SessionKind::Local
                ? m_localSession->Revision()
                : m_oidcSession->Revision();
        }

        if (kind == SessionKind::Local)
        {
            static_cast<void>(co_await m_localSession->ClearIfRevisionAsync(sessionRevision));
        }
        else
        {
            static_cast<void>(co_await m_oidcSession->ClearIfRevisionAsync(sessionRevision));
        }
        co_await wil::resume_foreground(m_dispatcher);

        RejectedHandler handler;
        {
            std::scoped_lock const lock{ m_mutex };
            if (m_generation != expectedGeneration || m_kind != kind)
            {
                co_return;
            }
            m_kind = SessionKind::None;
            ++m_generation;
            handler = m_rejectedHandler;
        }
        m_queryCache->Clear();
        if (handler)
        {
            handler();
        }
    }

    std::uint64_t SessionController::SessionGeneration() const noexcept
    {
        std::scoped_lock const lock{ m_mutex };
        return m_generation;
    }
}
