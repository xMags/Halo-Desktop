#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <ppltasks.h>
#include <winrt/HaloDesktop.h>
#include <winrt/Microsoft.UI.Dispatching.h>

namespace HaloDesktop::Api
{
    class ApiClient;
}

namespace HaloDesktop::Services
{
    // Resolves the landscape image a continue-watching card should draw. Watch state
    // remembers only the poster the player was started with, and a poster centre-
    // cropped into a 16:9 card is what the shelf showed before this existed. The
    // landscape image lives on the item's meta instead: the episode still for a
    // series, the backdrop for a film.
    //
    // UI-thread-only, like the catalog aggregator that drives it. Requests run on
    // background threads; the cache and every item update happen on the UI thread.
    class ContinueArtworkService final : public std::enable_shared_from_this<ContinueArtworkService>
    {
    public:
        ContinueArtworkService(
            std::shared_ptr<::HaloDesktop::Api::ApiClient> apiClient,
            winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher);

        // Retires whatever fill is in flight and returns the stamp the next one must
        // carry. A snapshot that has been replaced must not write to the new one.
        [[nodiscard]] std::uint64_t BeginGeneration() noexcept;

        // Gives every item its still, from cache where possible and from the item's
        // meta otherwise. Each item is updated as its own lookup returns, so the
        // caller does not have to await this before showing the shelf.
        [[nodiscard]] concurrency::task<void> FillAsync(
            std::vector<winrt::HaloDesktop::ContinueItem> items,
            std::uint64_t generation);

        void OnAccountChanged() noexcept;

    private:
        struct MetaArtwork final
        {
            winrt::hstring Background;
            std::unordered_map<std::wstring, winrt::hstring> Thumbnails;
        };

        [[nodiscard]] concurrency::task<void> FillItemAsync(
            winrt::HaloDesktop::ContinueItem item,
            std::uint64_t generation);
        static void Apply(winrt::HaloDesktop::ContinueItem const& item, MetaArtwork const& artwork);

        std::shared_ptr<::HaloDesktop::Api::ApiClient> m_apiClient;
        // Results come back on a worker thread and are handed to items the shelf
        // has already realised. Queueing them rather than resuming into the UI
        // apartment keeps each update between XAML's own work instead of nested
        // inside it, which is what a bound item mutated mid-layout cannot survive.
        winrt::Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };
        // Keyed by item id, which already carries the type and the meta id. Holds
        // failures as well as hits, so one unreachable addon cannot be retried on
        // every catalog refresh for the rest of the session.
        std::unordered_map<std::wstring, MetaArtwork> m_cache;
        std::uint64_t m_generation{};
    };
}
