#pragma once

#include "Api/Dto.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <ppltasks.h>
#include <unordered_map>
#include <winrt/Microsoft.UI.Dispatching.h>

namespace HaloDesktop::Api
{
    class ApiClient;
    class BackendSelector;
}

namespace HaloDesktop::Services
{
    using ServerFeaturesChangedToken = std::uint64_t;

    // What the account's server can do beyond the shared Halo contract, so a
    // page can hide what the server lacks (the older Halo server has none of
    // it). Asked once per account; until the answer lands, and whenever it
    // cannot be had, everything reads as unavailable.
    //
    // - Halo without an account: nothing is asked. People search and id
    //   lookups are offered while the viewer's own TMDB key works
    //   (DeviceTmdbChanged re-reads it); everything else is unavailable.
    // - A 404 or any other failure reads as nothing available, and the next
    //   EnsureLoaded (the next arrival on a page that needs it) asks again.
    //
    // UI-thread only. An account change retires the request in flight (an
    // account version) and forgets the answer. Handlers run on the UI thread
    // with no lock held; the coroutine reaches the service only weakly.
    class ServerFeaturesService final : public std::enable_shared_from_this<ServerFeaturesService>
    {
    public:
        using ChangedHandler = std::function<void()>;

        ServerFeaturesService(
            std::shared_ptr<::HaloDesktop::Api::ApiClient> api,
            std::shared_ptr<::HaloDesktop::Api::BackendSelector> backends,
            std::function<bool()> deviceTmdbUsable,
            winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher);

        ServerFeaturesService(ServerFeaturesService const&) = delete;
        ServerFeaturesService& operator=(ServerFeaturesService const&) = delete;

        // Asks the server unless the answer for this account is known or on
        // its way. Cheap to call on every arrival.
        void EnsureLoaded();
        [[nodiscard]] bool People() const noexcept;
        [[nodiscard]] bool Lookup() const noexcept;
        // Pasted magnets, for this account.
        [[nodiscard]] bool Magnets() const noexcept;
        // Whether the answer for this account is in (an answer that failed counts).
        [[nodiscard]] bool Settled() const noexcept;

        [[nodiscard]] ServerFeaturesChangedToken AddChangedHandler(ChangedHandler handler);
        void RemoveChangedHandler(ServerFeaturesChangedToken token) noexcept;
        void OnAccountChanged();
        // The TMDB key was saved, removed or refused: without an account,
        // what is offered follows it at once.
        void DeviceTmdbChanged();

    private:
        enum class State
        {
            Unknown,
            Loading,
            Loaded,
            Failed,
        };

        static concurrency::task<void> LoadAsync(
            std::weak_ptr<ServerFeaturesService> weak,
            std::shared_ptr<::HaloDesktop::Api::ApiClient> api,
            winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher,
            std::uint64_t accountVersion);
        void Apply(State state, ::HaloDesktop::Api::Dto::Capabilities capabilities);
        void NotifyChanged();

        std::shared_ptr<::HaloDesktop::Api::ApiClient> m_api;
        std::shared_ptr<::HaloDesktop::Api::BackendSelector> m_backends;
        std::function<bool()> m_deviceTmdbUsable;
        winrt::Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };
        std::uint64_t m_accountVersion{};
        State m_state{ State::Unknown };
        ::HaloDesktop::Api::Dto::Capabilities m_capabilities;
        std::unordered_map<ServerFeaturesChangedToken, ChangedHandler> m_handlers;
        ServerFeaturesChangedToken m_nextToken{ 1 };
    };
}
