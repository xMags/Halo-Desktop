#pragma once

#include <cstddef>
#include <memory>
#include <ppltasks.h>
#include <span>
#include <string>
#include <unordered_map>
#include <winrt/Microsoft.UI.Dispatching.h>

namespace HaloDesktop::Api::Addons
{
    class HttpAddonFetcher;
}

namespace HaloDesktop::Services
{
    // What is known of a poster link.
    enum class PosterLinkState
    {
        // Never asked, or asked and not answered yet.
        Unknown,
        // It answered, or could not be asked: taken as drawable.
        Present,
        // The host answered that there is no such image.
        Missing,
    };

    // Whether a poster link leads to an image, asked before Search lays its
    // results out: a catalog sends a link for every title, and for a little
    // known one the link can lead nowhere, which would put an empty frame
    // among the first cards. Only a host saying "no such image" counts as
    // missing; a slow or unreachable host is taken as present, so a network
    // fault never hides a title.
    //
    // UI-thread only. Answers are kept for the session, so a link is asked
    // once. The requests go through the app's addon fetcher: no cookies, no
    // credentials, http and https only. The coroutines reach the service only
    // weakly, so they may outlive it.
    class PosterCheckService final : public std::enable_shared_from_this<PosterCheckService>
    {
    public:
        PosterCheckService(
            std::shared_ptr<::HaloDesktop::Api::Addons::HttpAddonFetcher> fetcher,
            winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher);

        PosterCheckService(PosterCheckService const&) = delete;
        PosterCheckService& operator=(PosterCheckService const&) = delete;

        // Starts asking about every link not asked before.
        void Check(std::span<std::wstring const> links);
        [[nodiscard]] PosterLinkState StateOf(std::wstring const& link) const;

    private:
        static concurrency::task<void> AskAsync(
            std::weak_ptr<PosterCheckService> weak,
            std::shared_ptr<::HaloDesktop::Api::Addons::HttpAddonFetcher> fetcher,
            winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher,
            std::wstring link);

        std::shared_ptr<::HaloDesktop::Api::Addons::HttpAddonFetcher> m_fetcher;
        winrt::Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };
        // A link in flight is held as Unknown, so it is not asked twice.
        std::unordered_map<std::wstring, PosterLinkState> m_states;
    };
}
