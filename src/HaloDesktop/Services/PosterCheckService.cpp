#include "pch.h"
#include "Services/PosterCheckService.h"

#include "Api/Addons/AddonFetcher.h"
#include "Api/Addons/HttpAddonFetcher.h"

#include <chrono>
#include <pplawait.h>
#include <stdexcept>
#include <utility>
#include <wil/cppwinrt_helpers.h>

namespace
{
    // A poster the page is about to draw anyway; a host slower than this is
    // not waited for.
    constexpr std::chrono::milliseconds PosterBudget{ 2500 };
    constexpr std::size_t MaximumPosterBytes = 4 * 1024 * 1024;
    // Links remembered for the session before the memory starts over.
    constexpr std::size_t MaximumRemembered = 4000;
}

namespace HaloDesktop::Services
{
    PosterCheckService::PosterCheckService(
        std::shared_ptr<::HaloDesktop::Api::Addons::HttpAddonFetcher> fetcher,
        winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher)
        : m_fetcher(std::move(fetcher)), m_dispatcher(std::move(dispatcher))
    {
        if (!m_fetcher || !m_dispatcher)
        {
            throw std::invalid_argument{ "PosterCheckService requires a fetcher and a dispatcher." };
        }
    }

    void PosterCheckService::Check(std::span<std::wstring const> links)
    {
        for (auto const& link : links)
        {
            if (link.empty() || m_states.contains(link))
            {
                continue;
            }
            if (m_states.size() >= MaximumRemembered)
            {
                // A link in flight then answers into the new memory.
                m_states.clear();
            }
            m_states.emplace(link, PosterLinkState::Unknown);
            AskAsync(weak_from_this(), m_fetcher, m_dispatcher, link).then([](concurrency::task<void> finished)
            {
                try { finished.get(); } catch (...) {}
            });
        }
    }

    PosterLinkState PosterCheckService::StateOf(std::wstring const& link) const
    {
        auto const found = m_states.find(link);
        return found == m_states.end() ? PosterLinkState::Unknown : found->second;
    }

    concurrency::task<void> PosterCheckService::AskAsync(
        std::weak_ptr<PosterCheckService> weak,
        std::shared_ptr<::HaloDesktop::Api::Addons::HttpAddonFetcher> fetcher,
        winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher,
        std::wstring link)
    {
        auto state = PosterLinkState::Present;
        try
        {
            static_cast<void>(co_await fetcher->GetFileAsync(
                link, PosterBudget, MaximumPosterBytes, concurrency::cancellation_token::none()));
        }
        catch (::HaloDesktop::Api::Addons::AddonRequestFailure const& failure)
        {
            // Only the host's own "no" counts; anything else is unknown, and
            // unknown is drawn.
            auto const status = failure.Failure().Status;
            if (failure.Failure().Kind == ::HaloDesktop::Api::Addons::AddonFailureKind::UpstreamHttp
                && status && (*status == 404 || *status == 410))
            {
                state = PosterLinkState::Missing;
            }
        }
        catch (...)
        {
        }

        co_await wil::resume_foreground(dispatcher);
        if (auto const self = weak.lock())
        {
            self->m_states.insert_or_assign(std::move(link), state);
        }
    }
}
