#include "pch.h"
#include "Services/MagnetService.h"

#include "Api/ApiClient.h"
#include "Api/ApiError.h"
#include "Api/BackendSelector.h"
#include "Services/MagnetSourcePolicy.h"

#include <pplawait.h>
#include <stdexcept>
#include <utility>
#include <wil/cppwinrt_helpers.h>

namespace HaloDesktop::Services
{
    MagnetService::MagnetService(
        std::shared_ptr<::HaloDesktop::Api::ApiClient> api,
        std::shared_ptr<::HaloDesktop::Api::BackendSelector> backends,
        winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher)
        : m_api(std::move(api)), m_backends(std::move(backends)), m_dispatcher(std::move(dispatcher))
    {
        if (!m_api || !m_backends || !m_dispatcher)
        {
            throw std::invalid_argument{ "MagnetService requires the API client, a backend selector and a dispatcher." };
        }
    }

    MagnetService::~MagnetService()
    {
        if (m_pollTimer)
        {
            try { m_pollTimer.Stop(); } catch (...) {}
        }
    }

    void MagnetService::EnsureTimer()
    {
        if (m_pollTimer) return;
        m_pollTimer = m_dispatcher.CreateTimer();
        m_pollTimer.Interval(PollInterval);
        m_pollTimer.IsRepeating(false);
        m_pollTimer.Tick([weak = weak_from_this()](auto const&, auto const&)
        {
            if (auto const self = weak.lock()) self->OnPollTick();
        });
    }

    void MagnetService::Track(std::wstring infoHash, std::wstring name, Handler handler)
    {
        Stop();
        if (!handler) return;
        // Another magnet: the one kept for the Sources sheet goes.
        if (infoHash != m_resolvedHash)
        {
            m_resolved.reset();
            m_resolvedHash.clear();
        }
        m_handler = std::move(handler);
        m_hash = infoHash;
        m_trackedAt = std::chrono::steady_clock::now();
        Ask([api = m_api, hash = std::move(infoHash), name = std::move(name)]()
        {
            return api->AddMagnetAsync(winrt::hstring{ hash }, winrt::hstring{ name });
        }, true);
    }

    void MagnetService::Stop() noexcept
    {
        ++m_generation;
        if (m_pollTimer)
        {
            try { m_pollTimer.Stop(); } catch (...) {}
        }
        m_handler = nullptr;
        m_hash.clear();
        m_requestId.clear();
        m_asking = false;
        m_downloading = false;
    }

    void MagnetService::Forget()
    {
        auto id = m_requestId;
        Stop();
        if (id.empty() || m_backends->UsesDevice()) return;
        // Best effort: if the server cannot be reached, its daily clean-up
        // removes the torrent it added once nobody has played it for days.
        [](std::shared_ptr<::HaloDesktop::Api::ApiClient> api, winrt::hstring requestId) -> concurrency::task<void>
        {
            try { co_await api->ForgetMagnetAsync(std::move(requestId)); }
            catch (...) {}
        }(m_api, winrt::hstring{ id }).then([](concurrency::task<void> finished)
        {
            try { finished.get(); } catch (...) {}
        });
    }

    void MagnetService::SetWatching(bool watching) noexcept
    {
        if (m_watching == watching) return;
        m_watching = watching;
        if (!watching)
        {
            if (m_pollTimer)
            {
                try { m_pollTimer.Stop(); } catch (...) {}
            }
            return;
        }
        // A timer that cannot be made leaves the card on its last answer.
        try { SchedulePoll(); } catch (...) { m_downloading = false; }
    }

    void MagnetService::OnAccountChanged() noexcept
    {
        // The request and its play links belong to the old account's server.
        Stop();
        m_resolved.reset();
        m_resolvedHash.clear();
    }

    std::optional<::HaloDesktop::Api::Dto::MagnetFile> MagnetService::FileFor(std::wstring_view type, std::wstring_view videoId) const
    {
        if (!m_resolved) return std::nullopt;
        return MagnetFileFor(*m_resolved, type, videoId);
    }

    void MagnetService::Ask(std::function<concurrency::task<::HaloDesktop::Api::Dto::MagnetRequest>()> start, bool notFoundIsUnavailable)
    {
        auto const generation = m_generation;
        m_asking = true;
        // Never without an account: magnets live on the account's server.
        if (m_backends->UsesDevice())
        {
            m_dispatcher.TryEnqueue([weak = weak_from_this(), generation]()
            {
                if (auto const self = weak.lock()) self->OnAnswer(generation, MagnetOutcome::NotAvailable, std::nullopt);
            });
            return;
        }
        AskAsync(weak_from_this(), m_dispatcher, generation, std::move(start), notFoundIsUnavailable)
            .then([](concurrency::task<void> finished)
            {
                try { finished.get(); } catch (...) {}
            });
    }

    concurrency::task<void> MagnetService::AskAsync(
        std::weak_ptr<MagnetService> weak,
        winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher,
        std::uint64_t generation,
        std::function<concurrency::task<::HaloDesktop::Api::Dto::MagnetRequest>()> start,
        bool notFoundIsUnavailable)
    {
        std::optional<::HaloDesktop::Api::Dto::MagnetRequest> request;
        auto outcome = MagnetOutcome::Failed;
        try
        {
            request = co_await start();
            outcome = MagnetOutcome::Answered;
        }
        catch (winrt::hresult_error const& error)
        {
            auto const status = ::HaloDesktop::Api::ApiError::HttpStatus(error.code());
            outcome = notFoundIsUnavailable && status && *status == 404 ? MagnetOutcome::NotAvailable : MagnetOutcome::Failed;
        }
        catch (...)
        {
            // A bad answer (the mapper refused it) or a refused input.
            outcome = MagnetOutcome::Failed;
        }

        co_await wil::resume_foreground(dispatcher);
        if (auto const self = weak.lock())
        {
            self->OnAnswer(generation, outcome, std::move(request));
        }
    }

    void MagnetService::OnAnswer(
        std::uint64_t generation,
        MagnetOutcome outcome,
        std::optional<::HaloDesktop::Api::Dto::MagnetRequest> request)
    {
        if (generation != m_generation || !m_handler)
        {
            return;
        }
        m_asking = false;
        m_downloading = false;
        if (outcome == MagnetOutcome::Answered && request)
        {
            m_requestId = std::wstring{ request->Id };
            if (request->State == ::HaloDesktop::Api::Dto::MagnetState::Ready)
            {
                m_resolved = *request;
                m_resolvedHash = m_hash;
            }
            m_downloading = request->State == ::HaloDesktop::Api::Dto::MagnetState::Downloading;
        }
        // Copied: the handler may track another magnet or stop.
        auto const handler = m_handler;
        handler(outcome, std::move(request));
        if (generation == m_generation)
        {
            SchedulePoll();
        }
    }

    void MagnetService::SchedulePoll()
    {
        if (!m_downloading || !m_watching || m_asking || m_requestId.empty())
        {
            return;
        }
        if (std::chrono::steady_clock::now() - m_trackedAt >= PollLimit)
        {
            // Long enough: the card keeps its last answer, and pasting the
            // magnet again asks anew.
            m_downloading = false;
            return;
        }
        EnsureTimer();
        m_pollTimer.Start();
    }

    void MagnetService::OnPollTick()
    {
        if (!m_downloading || !m_watching || m_asking || m_requestId.empty())
        {
            return;
        }
        Ask([api = m_api, id = m_requestId]()
        {
            return api->GetMagnetAsync(winrt::hstring{ id });
        }, false);
    }
}
