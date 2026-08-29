#pragma once

#include "Services/ContinueShelfPolicy.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <ppltasks.h>
#include <winrt/Microsoft.UI.Dispatching.h>

namespace HaloDesktop::Api
{
    class ApiClient;
}

namespace HaloDesktop::Services
{
    // Answers "which episode follows this one" for the continue shelf, so a series
    // whose newest episode is finished advances instead of disappearing.
    //
    // The server already owns this question: /next-episode walks the meta's own
    // video ordering. Its addon and bingeGroup arguments are left off on purpose,
    // because those exist only to pre-match a stream, and the shelf opens the
    // source picker rather than playing anything directly.
    //
    // UI-thread-only, like the artwork fill it is shaped after. Requests run on
    // background threads; the cache and the completion callback happen on the
    // dispatcher thread.
    class ContinueNextEpisodeService final : public std::enable_shared_from_this<ContinueNextEpisodeService>
    {
    public:
        ContinueNextEpisodeService(
            std::shared_ptr<::HaloDesktop::Api::ApiClient> apiClient,
            winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher);

        // Retires whatever fill is in flight and returns the stamp the next one
        // must carry, so a snapshot that has been replaced cannot write into a
        // newer one.
        [[nodiscard]] std::uint64_t BeginGeneration() noexcept;

        // What is known about the episode after finishedVideoId. Unknown until a
        // resolve for it has landed, which is what makes the shelf ask.
        [[nodiscard]] NextEpisodeLookup Lookup(std::wstring const& finishedVideoId) const;

        // Resolves each request and commits it to the cache. onResolved runs on the
        // dispatcher thread once every request has settled, and only when at least
        // one produced an answer worth rebuilding the shelf for.
        [[nodiscard]] concurrency::task<void> ResolveAsync(
            std::vector<ContinueNextRequest> requests,
            std::uint64_t generation,
            std::function<void()> onResolved);

        void OnAccountChanged() noexcept;

    private:
        // True when an answer was committed. A lookup that failed commits nothing
        // and returns false: an unreachable addon is a reason to try again on the
        // next visit, not to hide the show until the app restarts.
        [[nodiscard]] concurrency::task<bool> ResolveOneAsync(
            ContinueNextRequest request,
            std::uint64_t generation);

        std::shared_ptr<::HaloDesktop::Api::ApiClient> m_apiClient;
        winrt::Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };
        // Keyed by the finished episode's video id, which is what the shelf holds
        // and is already unique per series position.
        std::unordered_map<std::wstring, NextEpisodeLookup> m_cache;
        std::uint64_t m_generation{};
    };
}
