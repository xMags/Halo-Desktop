#include "pch.h"
#include "Services/Auth/SessionController.h"

#include "Services/Auth/LocalAuthSession.h"
#include "Services/Auth/SessionStore.h"
#include "Services/QueryCache.h"

#include <stdexcept>
#include <utility>

namespace HaloDesktop::Services::Auth
{
    SessionController::SessionController(
        std::shared_ptr<SessionStore> store,
        std::shared_ptr<LocalAuthSession> localSession,
        std::shared_ptr<::HaloDesktop::Services::QueryCache> queryCache,
        winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher)
        : m_store(std::move(store)),
          m_localSession(std::move(localSession)),
          m_queryCache(std::move(queryCache)),
          m_dispatcher(std::move(dispatcher))
    {
        if (!m_store || !m_localSession || !m_queryCache || !m_dispatcher)
        {
            throw std::invalid_argument{ "SessionController requires all dependencies." };
        }
    }

    concurrency::task<void> SessionController::RestoreAsync()
    {
        co_await winrt::resume_background();
        auto const stored = co_await m_store->LoadAsync();
        if (!stored || stored->Kind != StoredSessionKind::Local || !stored->Local)
        {
            co_return;
        }

        m_localSession->Restore(*stored->Local);
        std::scoped_lock const lock{ m_mutex };
        m_kind = SessionKind::Local;
        ++m_generation;
    }

    concurrency::task<void> SessionController::SignInLocalAsync(
        winrt::hstring username,
        winrt::hstring password)
    {
        co_await winrt::resume_background();
        static_cast<void>(co_await m_localSession->LoginAsync(
            std::move(username),
            std::move(password)));
        std::scoped_lock const lock{ m_mutex };
        m_kind = SessionKind::Local;
        ++m_generation;
    }

    concurrency::task<void> SessionController::SignOutAsync()
    {
        co_await winrt::resume_background();
        {
            std::scoped_lock const lock{ m_mutex };
            m_kind = SessionKind::None;
            ++m_generation;
        }
        co_await m_localSession->ClearAsync();
        co_await wil::resume_foreground(m_dispatcher);
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
        if (Kind() != SessionKind::Local)
        {
            co_return std::nullopt;
        }
        co_return co_await m_localSession->AccessTokenAsync();
    }

    concurrency::task<std::optional<winrt::hstring>> SessionController::RefreshAccessTokenAsync()
    {
        co_await winrt::resume_background();
        if (Kind() != SessionKind::Local)
        {
            co_return std::nullopt;
        }
        co_return co_await m_localSession->RefreshAccessTokenAsync();
    }

    concurrency::task<void> SessionController::RejectSessionAsync(std::uint64_t expectedGeneration)
    {
        co_await winrt::resume_background();

        std::uint64_t localRevision{};
        {
            std::scoped_lock const lock{ m_mutex };
            if (m_generation != expectedGeneration || m_kind != SessionKind::Local)
            {
                co_return;
            }
            localRevision = m_localSession->Revision();
        }

        static_cast<void>(co_await m_localSession->ClearIfRevisionAsync(localRevision));
        co_await wil::resume_foreground(m_dispatcher);

        RejectedHandler handler;
        {
            std::scoped_lock const lock{ m_mutex };
            if (m_generation != expectedGeneration || m_kind != SessionKind::Local)
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
