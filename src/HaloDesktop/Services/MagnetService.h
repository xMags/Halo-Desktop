#pragma once

#include "Api/Dto.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <ppltasks.h>
#include <string>
#include <string_view>
#include <winrt/Microsoft.UI.Dispatching.h>

namespace HaloDesktop::Api
{
    class ApiClient;
    class BackendSelector;
}

namespace HaloDesktop::Services
{
    // How one ask about a pasted magnet ended.
    enum class MagnetOutcome
    {
        // The server answered; the request says where it stands.
        Answered,
        // Magnets are not available to this account (the server's 404 on
        // adding one), or Halo has no account.
        NotAvailable,
        // The server could not be reached, refused, or answered badly.
        Failed,
    };

    // Pasted magnets, on the account's server.
    //
    // Track asks about one magnet and, while the server is still fetching it,
    // asks again every 3 seconds, but only while the viewer watches its card
    // (SetWatching) and for at most 10 minutes from the paste. A new Track,
    // Stop, Forget or an account change retires the one before; a retired ask
    // never calls its handler.
    //
    // The last magnet that came back ready is kept until another magnet is
    // tracked, the account changes or the app closes, so the title page's
    // Sources sheet can offer its file (FileFor). Its files are never listed.
    //
    // UI-thread only. Handlers run on the UI thread; the coroutines and the
    // poll timer reach the service only weakly, so they may outlive it.
    class MagnetService final : public std::enable_shared_from_this<MagnetService>
    {
    public:
        using Handler = std::function<void(MagnetOutcome, std::optional<::HaloDesktop::Api::Dto::MagnetRequest>)>;

        static constexpr std::chrono::seconds PollInterval{ 3 };
        static constexpr std::chrono::minutes PollLimit{ 10 };

        MagnetService(
            std::shared_ptr<::HaloDesktop::Api::ApiClient> api,
            std::shared_ptr<::HaloDesktop::Api::BackendSelector> backends,
            winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher);
        ~MagnetService();

        MagnetService(MagnetService const&) = delete;
        MagnetService& operator=(MagnetService const&) = delete;

        // `infoHash` is 40 lower-case hex and `name` the magnet's display name
        // (SearchInputPolicy); only those are sent.
        void Track(std::wstring infoHash, std::wstring name, Handler handler);
        // Stops asking about the tracked magnet; what came back ready is kept.
        void Stop() noexcept;
        // Cancel on the card: the server forgets the tracked request (and the
        // torrent it added for it), and nothing more is asked.
        void Forget();
        // Whether the viewer is looking at the tracked magnet's card. Polling
        // pauses while not, and resumes (within the limit) when so again.
        void SetWatching(bool watching) noexcept;
        void OnAccountChanged() noexcept;

        // The file of the last ready magnet that stands for this video, if any
        // (MagnetSourcePolicy::MagnetFileFor).
        [[nodiscard]] std::optional<::HaloDesktop::Api::Dto::MagnetFile> FileFor(
            std::wstring_view type,
            std::wstring_view videoId) const;

    private:
        static concurrency::task<void> AskAsync(
            std::weak_ptr<MagnetService> weak,
            winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher,
            std::uint64_t generation,
            std::function<concurrency::task<::HaloDesktop::Api::Dto::MagnetRequest>()> start,
            bool notFoundIsUnavailable);
        void Ask(std::function<concurrency::task<::HaloDesktop::Api::Dto::MagnetRequest>()> start, bool notFoundIsUnavailable);
        void OnAnswer(std::uint64_t generation, MagnetOutcome outcome, std::optional<::HaloDesktop::Api::Dto::MagnetRequest> request);
        void SchedulePoll();
        void OnPollTick();
        void EnsureTimer();

        std::shared_ptr<::HaloDesktop::Api::ApiClient> m_api;
        std::shared_ptr<::HaloDesktop::Api::BackendSelector> m_backends;
        winrt::Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_pollTimer{ nullptr };

        std::uint64_t m_generation{};
        Handler m_handler;
        std::wstring m_hash;
        // The server's id for the tracked magnet, once it has answered.
        std::wstring m_requestId;
        std::chrono::steady_clock::time_point m_trackedAt{};
        bool m_watching{};
        bool m_asking{};
        bool m_downloading{};

        std::optional<::HaloDesktop::Api::Dto::MagnetRequest> m_resolved;
        std::wstring m_resolvedHash;
    };
}
