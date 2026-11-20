#include "pch.h"
#include "Services/ServerFeaturesService.h"

#include "Api/ApiClient.h"
#include "Api/BackendSelector.h"

#include <pplawait.h>
#include <stdexcept>
#include <utility>
#include <vector>
#include <wil/cppwinrt_helpers.h>

namespace HaloDesktop::Services
{
    ServerFeaturesService::ServerFeaturesService(
        std::shared_ptr<::HaloDesktop::Api::ApiClient> api,
        std::shared_ptr<::HaloDesktop::Api::BackendSelector> backends,
        std::function<bool()> deviceTmdbUsable,
        winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher)
        : m_api(std::move(api)),
          m_backends(std::move(backends)),
          m_deviceTmdbUsable(std::move(deviceTmdbUsable)),
          m_dispatcher(std::move(dispatcher))
    {
        if (!m_api || !m_backends || !m_deviceTmdbUsable || !m_dispatcher)
        {
            throw std::invalid_argument{ "ServerFeaturesService requires the API client, a backend selector, the TMDB key and a dispatcher." };
        }
    }

    void ServerFeaturesService::DeviceTmdbChanged()
    {
        // Only Halo without an account reads the key; an account's answer is the server's.
        if (!m_backends->UsesDevice() || m_state != State::Loaded) return;
        auto const usable = m_deviceTmdbUsable();
        Apply(State::Loaded, ::HaloDesktop::Api::Dto::Capabilities{ .People = usable, .Lookup = usable });
    }

    void ServerFeaturesService::EnsureLoaded()
    {
        if (m_state == State::Loading || m_state == State::Loaded)
        {
            return;
        }
        // These features live on the account's server; without an account
        // there is no server to ask, and people search and id lookups go
        // straight to TMDB while the viewer's own key works.
        if (m_backends->UsesDevice())
        {
            auto const usable = m_deviceTmdbUsable();
            Apply(State::Loaded, ::HaloDesktop::Api::Dto::Capabilities{ .People = usable, .Lookup = usable });
            return;
        }
        m_state = State::Loading;
        LoadAsync(weak_from_this(), m_api, m_dispatcher, m_accountVersion)
            .then([](concurrency::task<void> finished)
            {
                try { finished.get(); } catch (...) {}
            });
    }

    bool ServerFeaturesService::People() const noexcept
    {
        return m_state == State::Loaded && m_capabilities.People;
    }

    bool ServerFeaturesService::PeopleNeedTmdbKey() const noexcept
    {
        return m_state == State::Loaded && m_backends->UsesDevice() && !m_capabilities.People;
    }

    bool ServerFeaturesService::Lookup() const noexcept
    {
        return m_state == State::Loaded && m_capabilities.Lookup;
    }

    bool ServerFeaturesService::Magnets() const noexcept
    {
        return m_state == State::Loaded && m_capabilities.Magnets;
    }

    bool ServerFeaturesService::Settled() const noexcept
    {
        return m_state == State::Loaded || m_state == State::Failed;
    }

    ServerFeaturesChangedToken ServerFeaturesService::AddChangedHandler(ChangedHandler handler)
    {
        auto const token = m_nextToken++;
        m_handlers.emplace(token, std::move(handler));
        return token;
    }

    void ServerFeaturesService::RemoveChangedHandler(ServerFeaturesChangedToken token) noexcept
    {
        m_handlers.erase(token);
    }

    void ServerFeaturesService::OnAccountChanged()
    {
        ++m_accountVersion;
        auto const wasKnown = m_state != State::Unknown;
        m_state = State::Unknown;
        m_capabilities = {};
        if (wasKnown)
        {
            NotifyChanged();
        }
    }

    concurrency::task<void> ServerFeaturesService::LoadAsync(
        std::weak_ptr<ServerFeaturesService> weak,
        std::shared_ptr<::HaloDesktop::Api::ApiClient> api,
        winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher,
        std::uint64_t accountVersion)
    {
        std::optional<::HaloDesktop::Api::Dto::Capabilities> answer;
        try
        {
            answer = co_await api->GetCapabilitiesAsync();
        }
        catch (...)
        {
            // A server without the endpoint (404), or one that could not be
            // reached: nothing is offered until the next arrival asks again.
            answer.reset();
        }

        co_await wil::resume_foreground(dispatcher);
        auto const self = weak.lock();
        if (!self || self->m_accountVersion != accountVersion || self->m_state != State::Loading)
        {
            co_return;
        }
        if (answer)
        {
            self->Apply(State::Loaded, *answer);
        }
        else
        {
            self->Apply(State::Failed, {});
        }
    }

    void ServerFeaturesService::Apply(State state, ::HaloDesktop::Api::Dto::Capabilities capabilities)
    {
        auto const changed = !(capabilities == m_capabilities) || Settled() != (state == State::Loaded || state == State::Failed);
        m_state = state;
        m_capabilities = capabilities;
        if (changed)
        {
            NotifyChanged();
        }
    }

    void ServerFeaturesService::NotifyChanged()
    {
        // Copied first: a handler may add or remove handlers.
        std::vector<ChangedHandler> handlers;
        handlers.reserve(m_handlers.size());
        for (auto const& entry : m_handlers)
        {
            handlers.push_back(entry.second);
        }
        for (auto const& handler : handlers)
        {
            if (handler) handler();
        }
    }
}
