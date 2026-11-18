#include "pch.h"
#include "Services/Auth/SessionController.h"

#include "Services/Auth/LocalAuthSession.h"
#include "Services/Auth/OidcAuthSession.h"
#include "Services/Auth/ServerAddressPolicy.h"
#include "Services/Auth/SessionStore.h"
#include "Services/Auth/SignInAddonRegistry.h"
#include "Services/QueryCache.h"

#include <stdexcept>
#include <utility>

namespace
{
    // A server recorded in the protected session file is still checked: only
    // an address the sign-in form would accept is ever talked to.
    bool IsUsableServer(winrt::hstring const& serverUrl)
    {
        auto const parsed = ::HaloDesktop::Services::Auth::ParseServerAddress(std::wstring_view{ serverUrl });
        return parsed.Address && *parsed.Address == std::wstring_view{ serverUrl };
    }
}

namespace HaloDesktop::Services::Auth
{
    SessionController::SessionController(
        std::shared_ptr<SessionStore> store,
        std::shared_ptr<LocalAuthSession> localSession,
        std::shared_ptr<OidcAuthSession> oidcSession,
        std::shared_ptr<SignInAddonRegistry const> addons,
        std::shared_ptr<::HaloDesktop::Services::QueryCache> queryCache,
        winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher)
        : m_store(std::move(store)),
          m_localSession(std::move(localSession)),
          m_oidcSession(std::move(oidcSession)),
          m_addons(std::move(addons)),
          m_queryCache(std::move(queryCache)),
          m_dispatcher(std::move(dispatcher))
    {
        if (!m_store || !m_localSession || !m_oidcSession || !m_addons || !m_queryCache || !m_dispatcher)
        {
            throw std::invalid_argument{ "SessionController requires all dependencies." };
        }
    }

    concurrency::task<void> SessionController::RestoreAsync(bool forgetBuiltInSessions)
    {
        co_await winrt::resume_background();
        auto const stored = co_await m_store->LoadAsync();
        auto restored = false;
        auto wipeStored = false;
        if (stored && stored->Kind == StoredSessionKind::Local && stored->Local)
        {
            auto const& serverUrl = stored->Local->ServerUrl;
            if (serverUrl && !IsUsableServer(*serverUrl))
            {
                wipeStored = true;
            }
            else if (!serverUrl && forgetBuiltInSessions)
            {
                // Saved before sessions recorded their server, so it belongs to
                // whichever built-in server the build that saved it had.
                wipeStored = true;
            }
            else
            {
                m_localSession->Restore(*stored->Local);
                std::scoped_lock const lock{ m_mutex };
                m_kind = SessionKind::Local;
                ++m_generation;
                restored = true;
            }
        }
        else if (stored && stored->Kind == StoredSessionKind::Oidc && stored->Oidc)
        {
            if (forgetBuiltInSessions)
            {
                wipeStored = true;
            }
            else
            {
                m_oidcSession->Restore(*stored->Oidc);
                std::scoped_lock const lock{ m_mutex };
                m_kind = SessionKind::Oidc;
                ++m_generation;
                restored = true;
            }
        }
        if (wipeStored)
        {
            try
            {
                co_await m_store->ClearAsync();
            }
            catch (...)
            {
                // Not restored either way; the next start tries again.
            }
        }

        for (auto const& addon : m_addons->All())
        {
            if (forgetBuiltInSessions)
            {
                try
                {
                    co_await addon->ClearAsync();
                }
                catch (...)
                {
                }
                continue;
            }
            if (restored)
            {
                continue;
            }
            auto addonRestored = false;
            try
            {
                addonRestored = co_await addon->RestoreAsync();
            }
            catch (...)
            {
                addonRestored = false;
            }
            if (addonRestored)
            {
                std::scoped_lock const lock{ m_mutex };
                m_kind = SessionKind::Addon;
                m_activeAddon = addon;
                ++m_generation;
                restored = true;
            }
        }
    }

    concurrency::task<std::uint64_t> SessionController::SignInLocalAsync(
        winrt::hstring serverUrl,
        winrt::hstring username,
        winrt::hstring password)
    {
        co_await winrt::resume_background();
        co_await m_oidcSession->SignOutAsync(false);
        static_cast<void>(co_await m_localSession->LoginAsync(
            std::move(serverUrl),
            std::move(username),
            std::move(password)));
        std::scoped_lock const lock{ m_mutex };
        m_kind = SessionKind::Local;
        m_activeAddon.reset();
        co_return ++m_generation;
    }

    concurrency::task<std::uint64_t> SessionController::SignInOidcAsync(StoredOidcSession session)
    {
        co_await winrt::resume_background();
        co_await m_localSession->ClearAsync();
        co_await m_oidcSession->EstablishAsync(std::move(session));
        std::scoped_lock const lock{ m_mutex };
        m_kind = SessionKind::Oidc;
        m_activeAddon.reset();
        co_return ++m_generation;
    }

    concurrency::task<std::uint64_t> SessionController::SignInAddonAsync(
        std::shared_ptr<ISignInAddon> addon,
        winrt::hstring serverUrl,
        AddonAuthConfig config,
        SignInOpenedHandler onOpened,
        std::stop_token cancellation)
    {
        if (!addon)
        {
            throw std::invalid_argument{ "A sign-in add-on is required." };
        }
        co_await winrt::resume_background();
        co_await addon->SignInAsync(std::move(serverUrl), std::move(config), std::move(onOpened), cancellation);
        // The add-on has persisted its session; the built-in kinds' saved
        // sessions would otherwise be restored ahead of it next launch.
        co_await m_localSession->ClearAsync();
        co_await m_oidcSession->SignOutAsync(false);
        std::scoped_lock const lock{ m_mutex };
        m_kind = SessionKind::Addon;
        m_activeAddon = std::move(addon);
        co_return ++m_generation;
    }

    concurrency::task<void> SessionController::SignOutAsync()
    {
        co_await winrt::resume_background();
        SessionKind kind{};
        std::uint64_t generation{};
        std::shared_ptr<ISignInAddon> addon;
        {
            std::scoped_lock const lock{ m_mutex };
            kind = m_kind;
            generation = m_generation;
            addon = m_activeAddon;
        }
        if (kind == SessionKind::Local)
        {
            co_await m_localSession->ClearAsync();
        }
        else if (kind == SessionKind::Oidc)
        {
            co_await m_oidcSession->SignOutAsync(true);
        }
        else if (kind == SessionKind::Addon && addon)
        {
            co_await addon->SignOutAsync();
        }
        co_await wil::resume_foreground(m_dispatcher);
        {
            std::scoped_lock const lock{ m_mutex };
            if (m_kind != kind || m_generation != generation)
            {
                co_return;
            }
            m_kind = SessionKind::None;
            m_activeAddon.reset();
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

    std::wstring SessionController::KindName() const
    {
        std::scoped_lock const lock{ m_mutex };
        switch (m_kind)
        {
        case SessionKind::Local:
            return L"local";
        case SessionKind::Oidc:
            return L"oidc";
        case SessionKind::Addon:
            return m_activeAddon ? AddonSessionKind(m_activeAddon->Mode()) : std::wstring{};
        case SessionKind::None:
            break;
        }
        return {};
    }

    void SessionController::SetRejectedHandler(RejectedHandler handler)
    {
        std::scoped_lock const lock{ m_mutex };
        m_rejectedHandler = std::move(handler);
    }

    std::shared_ptr<ISignInAddon> SessionController::ActiveAddon() const
    {
        std::scoped_lock const lock{ m_mutex };
        return m_kind == SessionKind::Addon ? m_activeAddon : nullptr;
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
        if (kind == SessionKind::Addon)
        {
            auto const addon = ActiveAddon();
            if (!addon)
            {
                co_return std::nullopt;
            }
            co_return co_await addon->AccessTokenAsync();
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
        if (kind == SessionKind::Addon)
        {
            auto const addon = ActiveAddon();
            if (!addon)
            {
                co_return std::nullopt;
            }
            co_return co_await addon->RefreshAccessTokenAsync();
        }
        co_return co_await m_oidcSession->RefreshAccessTokenAsync();
    }

    concurrency::task<void> SessionController::RejectSessionAsync(std::uint64_t expectedGeneration)
    {
        co_await winrt::resume_background();

        std::uint64_t sessionRevision{};
        SessionKind kind{ SessionKind::None };
        std::shared_ptr<ISignInAddon> addon;
        {
            std::scoped_lock const lock{ m_mutex };
            if (m_generation != expectedGeneration || m_kind == SessionKind::None)
            {
                co_return;
            }
            kind = m_kind;
            addon = m_activeAddon;
            if (kind == SessionKind::Local)
            {
                sessionRevision = m_localSession->Revision();
            }
            else if (kind == SessionKind::Oidc)
            {
                sessionRevision = m_oidcSession->Revision();
            }
        }

        if (kind == SessionKind::Local)
        {
            static_cast<void>(co_await m_localSession->ClearIfRevisionAsync(sessionRevision));
        }
        else if (kind == SessionKind::Oidc)
        {
            static_cast<void>(co_await m_oidcSession->ClearIfRevisionAsync(sessionRevision));
        }
        else if (addon)
        {
            co_await addon->ClearAsync();
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
            m_activeAddon.reset();
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

    SessionTarget SessionController::CurrentTarget() const
    {
        std::scoped_lock const lock{ m_mutex };
        SessionTarget target{ .Generation = m_generation, .ServerUrl = std::nullopt };
        if (m_kind == SessionKind::Local)
        {
            target.ServerUrl = m_localSession->ServerUrl();
        }
        return target;
    }
}
