#pragma once

#include "Services/SearchResultsPolicy.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <ppltasks.h>
#include <string>
#include <winrt/Microsoft.UI.Dispatching.h>

namespace HaloDesktop::Api
{
    class BackendSelector;
}

namespace HaloDesktop::Services
{
    // Finds the title a pasted IMDb id names, through the current backend
    // (the account's server, or this PC's backend without an account).
    //
    // The server needs a type with every meta call, and an IMDb id does not
    // say which, so a film and a series are asked for at the same time. An
    // answer whose own type is not what was asked is not counted. When both
    // count, a series with episodes beats the film; otherwise the film wins.
    //
    // UI-thread only. One lookup at a time: a new Lookup, Cancel or an
    // account change retires the one in flight (a generation counter), and a
    // retired lookup never calls its handler. The coroutine holds the service
    // only weakly, so it may outlive the service.
    class TitleLookupService final : public std::enable_shared_from_this<TitleLookupService>
    {
    public:
        // Called on the UI thread with the title, or nothing when no addon
        // knows the id (or every call failed).
        using Handler = std::function<void(std::optional<SearchTitle>)>;

        TitleLookupService(
            std::shared_ptr<::HaloDesktop::Api::BackendSelector> backends,
            winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher);

        TitleLookupService(TitleLookupService const&) = delete;
        TitleLookupService& operator=(TitleLookupService const&) = delete;

        void Lookup(std::wstring imdbId, Handler handler);
        void Cancel() noexcept;
        void OnAccountChanged() noexcept;

    private:
        static concurrency::task<void> RunAsync(
            std::weak_ptr<TitleLookupService> weak,
            std::shared_ptr<::HaloDesktop::Api::BackendSelector> backends,
            winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher,
            std::uint64_t generation,
            std::wstring imdbId,
            Handler handler);

        std::shared_ptr<::HaloDesktop::Api::BackendSelector> m_backends;
        winrt::Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };
        std::uint64_t m_generation{};
    };
}
