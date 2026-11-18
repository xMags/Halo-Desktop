#pragma once

#include "Api/Dto.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>
#include <pplawait.h>
#include <ppltasks.h>
#include <winrt/Windows.Data.Json.h>

namespace HaloDesktop::Api
{
    // Everything the app's services ask of a backend, as the reference client's
    // `HaloBackend` names it: the account's ApiClient answers it from the
    // server, and the on-device backend answers it on this PC for Halo used
    // without an account. Account-only calls (who is signed in, the profile
    // picture, the admin's global addon list, health, sign-in discovery and the
    // addon proxy) stay on ApiClient and are not here.
    //
    // Both implementations fail the same way: an HTTP refusal is a
    // winrt::hresult_error with ApiError::MakeHttpStatus(status) and the
    // server's `error` sentence as its message, so a service cannot tell them
    // apart. Every method is safe to call from any thread and leaves the
    // caller's apartment before doing network or disk work.
    class IHaloBackend
    {
    public:
        virtual ~IHaloBackend() = default;

        [[nodiscard]] virtual concurrency::task<Dto::AddonsPayload> GetAddonsAsync() = 0;
        // Declares the caller's own addon list, in priority order.
        [[nodiscard]] virtual concurrency::task<void> PutAddonsAsync(std::vector<winrt::hstring> transportUrls) = 0;
        // The caller's own addon only; the admin's global list is account-only.
        [[nodiscard]] virtual concurrency::task<void> PatchAddonAsync(winrt::hstring addonId, bool hideCatalogs) = 0;
        [[nodiscard]] virtual concurrency::task<std::vector<Dto::MetaPreview>> GetCatalogAsync(
            winrt::hstring addonId,
            winrt::hstring type,
            winrt::hstring catalogId,
            std::vector<std::pair<winrt::hstring, winrt::hstring>> extras = {}) = 0;
        [[nodiscard]] virtual concurrency::task<Dto::MetaDetail> GetMetaAsync(
            winrt::hstring type,
            winrt::hstring metaId) = 0;
        [[nodiscard]] virtual concurrency::task<Dto::StreamsPayload> GetStreamsAsync(
            winrt::hstring type,
            winrt::hstring videoId) = 0;
        // An empty addon id or binge group is left out, as the query string
        // leaves it out.
        [[nodiscard]] virtual concurrency::task<Dto::NextEpisodePayload> GetNextEpisodeAsync(
            winrt::hstring type,
            winrt::hstring metaId,
            winrt::hstring videoId,
            winrt::hstring addonId,
            winrt::hstring bingeGroup) = 0;
        [[nodiscard]] virtual concurrency::task<Dto::SubtitlesPayload> GetSubtitlesAsync(
            winrt::hstring type,
            winrt::hstring videoId,
            std::optional<winrt::hstring> videoHash,
            std::optional<std::uint64_t> videoSize,
            std::optional<winrt::hstring> filename) = 0;
        // Every stored row, removals included.
        [[nodiscard]] virtual concurrency::task<std::vector<Dto::LibraryRow>> GetLibraryAsync() = 0;
        // Answers every row stored after the merge.
        [[nodiscard]] virtual concurrency::task<std::vector<Dto::LibraryRow>> PutLibraryAsync(
            std::vector<Dto::LibraryRow> rows) = 0;
        // The live watch history: removal tombstones are never returned.
        [[nodiscard]] virtual concurrency::task<std::vector<Dto::WatchEntry>> GetWatchStateAsync() = 0;
        // Rows may be removal tombstones (RemovedAt set); answers the live
        // history after the merge.
        [[nodiscard]] virtual concurrency::task<std::vector<Dto::WatchEntry>> PutWatchStateAsync(
            std::vector<Dto::WatchEntry> rows) = 0;
        [[nodiscard]] virtual concurrency::task<Dto::SettingsPayload> GetSettingsAsync() = 0;
        // Stored only when strictly newer; answers whatever is stored afterwards.
        [[nodiscard]] virtual concurrency::task<Dto::SettingsPayload> PutSettingsAsync(
            winrt::Windows::Data::Json::JsonObject value,
            std::int64_t updatedAt) = 0;

    protected:
        IHaloBackend() = default;
        IHaloBackend(IHaloBackend const&) = default;
        IHaloBackend& operator=(IHaloBackend const&) = default;
    };
}
