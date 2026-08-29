#include "pch.h"
#include "Services/ContinueNextEpisodeService.h"

#include "Api/ApiClient.h"

#include <pplawait.h>
#include <stdexcept>
#include <utility>
#include <wil/cppwinrt_helpers.h>

namespace HaloDesktop::Services
{
    ContinueNextEpisodeService::ContinueNextEpisodeService(
        std::shared_ptr<::HaloDesktop::Api::ApiClient> apiClient,
        winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher)
        : m_apiClient(std::move(apiClient)), m_dispatcher(std::move(dispatcher))
    {
        if (!m_apiClient || !m_dispatcher)
        {
            throw std::invalid_argument{ "ContinueNextEpisodeService requires an API client and a dispatcher." };
        }
    }

    std::uint64_t ContinueNextEpisodeService::BeginGeneration() noexcept
    {
        return ++m_generation;
    }

    NextEpisodeLookup ContinueNextEpisodeService::Lookup(std::wstring const& finishedVideoId) const
    {
        auto const cached = m_cache.find(finishedVideoId);
        return cached != m_cache.end() ? cached->second : NextEpisodeLookup{};
    }

    concurrency::task<void> ContinueNextEpisodeService::ResolveAsync(
        std::vector<ContinueNextRequest> requests,
        std::uint64_t generation,
        std::function<void()> onResolved)
    {
        // Holds the service up for as long as the fill runs, so nothing below can
        // land on a destroyed cache during shutdown.
        auto const lifetime = shared_from_this();
        if (requests.empty())
        {
            co_return;
        }

        // The cache and the generation stamp belong to the dispatcher thread, and
        // a caller is not required to already be on it.
        if (!m_dispatcher.HasThreadAccess())
        {
            co_await wil::resume_foreground(m_dispatcher);
        }

        std::vector<concurrency::task<bool>> lookups;
        lookups.reserve(requests.size());
        for (auto const& request : requests)
        {
            lookups.push_back(ResolveOneAsync(request, generation));
        }

        // One series failing says nothing about the rest, and a show that keeps
        // its place off the shelf is the behaviour this replaced rather than a
        // broken shelf.
        auto committed = false;
        for (auto& lookup : lookups)
        {
            try
            {
                if (co_await lookup)
                {
                    committed = true;
                }
            }
            catch (...)
            {
            }
        }

        if (!committed || !onResolved)
        {
            co_return;
        }
        co_await wil::resume_foreground(m_dispatcher);
        if (generation != m_generation)
        {
            co_return;
        }
        onResolved();
    }

    concurrency::task<bool> ContinueNextEpisodeService::ResolveOneAsync(
        ContinueNextRequest request,
        std::uint64_t generation)
    {
        auto const lifetime = shared_from_this();

        NextEpisodeLookup resolved{ NextEpisodeState::None, {} };
        auto answered = false;
        try
        {
            // addon and bingeGroup are deliberately empty. They only pre-match a
            // stream for the player, and ApiClient omits empty values from the
            // query, which is exactly the metadata-only call this wants.
            auto const payload = co_await m_apiClient->GetNextEpisodeAsync(
                winrt::hstring{ request.Type },
                winrt::hstring{ request.MetaId },
                winrt::hstring{ request.VideoId },
                winrt::hstring{},
                winrt::hstring{});
            if (payload.Video && !payload.Video->Id.empty())
            {
                resolved = { NextEpisodeState::Resolved, std::wstring{ payload.Video->Id } };
            }
            // An absent video is the series having no next episode. That is an
            // answer, and caching it is what stops the shelf asking again.
            answered = true;
        }
        catch (...)
        {
            // Left unanswered on purpose. Caching a failure would hide the show
            // for the rest of the session over one unreachable addon.
        }

        if (!answered)
        {
            co_return false;
        }

        co_await wil::resume_foreground(m_dispatcher);
        if (generation != m_generation)
        {
            co_return false;
        }
        m_cache.insert_or_assign(request.VideoId, std::move(resolved));
        co_return true;
    }

    void ContinueNextEpisodeService::OnAccountChanged() noexcept
    {
        ++m_generation;
        m_cache.clear();
    }
}
