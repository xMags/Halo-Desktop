#include "pch.h"
#include "Services/Tmdb/TmdbKeyStore.h"

#include "Security/Dpapi.h"
#include "Services/Tmdb/TmdbHttp.h"

#include <pplawait.h>
#include <stdexcept>
#include <utility>
#include <vector>
#include <wil/cppwinrt_helpers.h>

namespace
{
    using HaloDesktop::Services::Tmdb::TmdbCredential;
    using HaloDesktop::Services::Tmdb::TmdbCredentialKind;

    // The stored form: the kind, a line break, then the credential.
    std::string Serialized(TmdbCredential const& credential)
    {
        auto text = winrt::to_string(credential.Value);
        return (credential.Kind == TmdbCredentialKind::ApiKey ? "key\n" : "token\n") + text;
    }

    std::optional<TmdbCredential> Deserialized(std::string const& stored)
    {
        auto const split = stored.find('\n');
        if (split == std::string::npos) return std::nullopt;
        auto const kind = stored.substr(0, split);
        auto parsed = HaloDesktop::Services::Tmdb::ParseTmdbCredential(winrt::to_hstring(stored.substr(split + 1)));
        if (!parsed) return std::nullopt;
        auto const expected = kind == "key" ? TmdbCredentialKind::ApiKey : TmdbCredentialKind::ReadToken;
        if ((kind != "key" && kind != "token") || parsed->Kind != expected) return std::nullopt;
        return parsed;
    }
}

namespace HaloDesktop::Services::Tmdb
{
    TmdbKeyStore::TmdbKeyStore(std::filesystem::path file, winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher)
        : m_file(std::move(file)), m_dispatcher(std::move(dispatcher))
    {
        if (m_file.empty() || !m_dispatcher)
        {
            throw std::invalid_argument{ "TmdbKeyStore requires a file and a dispatcher." };
        }
    }

    concurrency::task<void> TmdbKeyStore::LoadAsync()
    {
        auto const self = shared_from_this();
        std::optional<TmdbCredential> loaded;
        try
        {
            if (auto const stored = co_await ::HaloDesktop::Security::ReadProtectedTextAsync(m_file, 8192))
            {
                loaded = Deserialized(*stored);
            }
        }
        catch (...)
        {
            // Unreadable (another Windows user's file, or damaged): no key.
        }
        co_await wil::resume_foreground(m_dispatcher);
        {
            std::scoped_lock const lock{ m_mutex };
            m_credential = std::move(loaded);
            m_refused = false;
        }
        NotifyChanged();
    }

    concurrency::task<TmdbKeySave> TmdbKeyStore::SaveAsync(std::wstring pasted)
    {
        auto const self = shared_from_this();
        auto const credential = ParseTmdbCredential(pasted);
        if (!credential)
        {
            co_return TmdbKeySave::NotAKey;
        }
        // The cheapest request TMDB answers for any working credential.
        std::uint16_t status{};
        bool reached{};
        try
        {
            status = (co_await GetTmdbJsonAsync(m_client, *credential, L"/3/configuration")).Status;
            reached = true;
        }
        catch (...)
        {
        }
        auto outcome = TmdbKeySave::Saved;
        if (!reached || (status != 200 && status != 401))
        {
            outcome = TmdbKeySave::Unreachable;
        }
        else if (status == 401)
        {
            outcome = TmdbKeySave::Refused;
        }
        else
        {
            try
            {
                co_await ::HaloDesktop::Security::WriteProtectedTextAsync(m_file, Serialized(*credential));
            }
            catch (...)
            {
                outcome = TmdbKeySave::NotStored;
            }
        }
        co_await wil::resume_foreground(m_dispatcher);
        if (outcome != TmdbKeySave::Saved)
        {
            co_return outcome;
        }
        {
            std::scoped_lock const lock{ m_mutex };
            m_credential = *credential;
            m_refused = false;
        }
        NotifyChanged();
        co_return TmdbKeySave::Saved;
    }

    concurrency::task<void> TmdbKeyStore::RemoveAsync()
    {
        auto const self = shared_from_this();
        {
            std::scoped_lock const lock{ m_mutex };
            m_credential.reset();
            m_refused = false;
        }
        NotifyChanged();
        try
        {
            co_await ::HaloDesktop::Security::DeleteProtectedFileAsync(m_file);
        }
        catch (...)
        {
            // The key is already out of use; a file left behind is read again
            // only at the next launch, and a refused one is then refused again.
        }
        co_await wil::resume_foreground(m_dispatcher);
    }

    std::optional<TmdbCredential> TmdbKeyStore::Current() const
    {
        std::scoped_lock const lock{ m_mutex };
        if (!m_credential || m_refused) return std::nullopt;
        return m_credential;
    }

    bool TmdbKeyStore::HasKey() const
    {
        std::scoped_lock const lock{ m_mutex };
        return m_credential.has_value();
    }

    bool TmdbKeyStore::IsRefused() const
    {
        std::scoped_lock const lock{ m_mutex };
        return m_credential && m_refused;
    }

    void TmdbKeyStore::MarkRefused(TmdbCredential const& credential) noexcept
    {
        try
        {
            {
                std::scoped_lock const lock{ m_mutex };
                if (!m_credential || m_refused || m_credential->Value != credential.Value) return;
                m_refused = true;
            }
            m_dispatcher.TryEnqueue([weak = weak_from_this()]()
            {
                if (auto const self = weak.lock()) self->NotifyChanged();
            });
        }
        catch (...)
        {
        }
    }

    TmdbKeyChangedToken TmdbKeyStore::AddChangedHandler(ChangedHandler handler)
    {
        auto const token = m_nextToken++;
        m_handlers.emplace(token, std::move(handler));
        return token;
    }

    void TmdbKeyStore::RemoveChangedHandler(TmdbKeyChangedToken token) noexcept
    {
        m_handlers.erase(token);
    }

    void TmdbKeyStore::NotifyChanged()
    {
        // Copied first: a handler may add or remove handlers.
        std::vector<ChangedHandler> handlers;
        handlers.reserve(m_handlers.size());
        for (auto const& [token, handler] : m_handlers) handlers.push_back(handler);
        for (auto const& handler : handlers)
        {
            if (handler) handler();
        }
    }
}
