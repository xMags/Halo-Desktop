#pragma once

#include "Api/Addons/AddonClientPolicy.h"
#include "Api/Addons/AddonFetcher.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <pplawait.h>
#include <ppltasks.h>
#include <winrt/Windows.Data.Json.h>

namespace HaloDesktop::Api::Addons
{
    // Addon resolution as the reference's `addon/resolve.ts` runs it, which is
    // also what the server runs for a signed-in user: which installed addons
    // to ask, in what order, and how their answers combine. Every answer is
    // the JSON the server would send, so the app's mappers read it unchanged.
    //
    // Each function takes its arguments by value and keeps the fetcher alive
    // through the context, so a call never depends on its caller's lifetime.
    // Every addon request has its own budget (ResolveContext::Timeout); an
    // addon that fails or times out is reported or skipped, never fatal,
    // except where the reference says so. A cancelled context ends the call
    // with winrt::hresult_canceled.

    // An installed addon as resolution needs it.
    struct ResolvableAddon final
    {
        std::wstring Id;
        std::wstring TransportUrl;
        winrt::Windows::Data::Json::JsonObject Manifest{ nullptr };
        ManifestFacts Facts;
    };

    [[nodiscard]] ResolvableAddon MakeResolvableAddon(
        std::wstring id,
        std::wstring transportUrl,
        winrt::Windows::Data::Json::JsonObject manifest);

    struct ResolveContext final
    {
        std::shared_ptr<IAddonFetcher> Fetcher;
        // Budget for each addon request.
        std::chrono::milliseconds Timeout{ ResolveTimeout };
        concurrency::cancellation_token Cancellation{ concurrency::cancellation_token::none() };
        // Milliseconds since the epoch, for hiding unreleased episodes; the
        // system clock when empty.
        std::function<std::int64_t()> Now;
    };

    // One catalog from one addon, without the entries that are actions rather
    // than titles: the addon's answer with `metas` filtered. Throws
    // AddonRequestFailure when the addon fails.
    [[nodiscard]] concurrency::task<winrt::Windows::Data::Json::JsonObject> FetchCatalogAsync(
        ResolvableAddon addon,
        std::wstring type,
        std::wstring id,
        ExtraArgs extra,
        ResolveContext context);

    // The answer (`{ meta, ... }`) of the first addon, in order, that can
    // describe the type and id and does; empty when none does.
    [[nodiscard]] concurrency::task<std::optional<winrt::Windows::Data::Json::JsonObject>> ResolveMetaAsync(
        std::vector<ResolvableAddon> addons,
        std::wstring type,
        std::wstring id,
        ResolveContext context);

    // ResolveMetaAsync, plus the pictured cast of an installed TMDB addon
    // (manifest id `tmdb-addon`) for an IMDb id when the answering addon sent
    // none. Both requests run at once; the cast never fails the meta.
    [[nodiscard]] concurrency::task<std::optional<winrt::Windows::Data::Json::JsonObject>> ResolveMetaWithCastAsync(
        std::vector<ResolvableAddon> addons,
        std::wstring type,
        std::wstring id,
        ResolveContext context);

    // `{ results: [{ addon: { id, name }, streams }], errors: [...] }`: every
    // stream-capable addon asked at once; addons with no playable stream are
    // left out of the results, failures are reported by code.
    [[nodiscard]] concurrency::task<winrt::Windows::Data::Json::JsonObject> ResolveStreamsAsync(
        std::vector<ResolvableAddon> addons,
        std::wstring type,
        std::wstring videoId,
        ResolveContext context);

    // `{ results, errors, hashMatched }`: every subtitle-capable addon asked
    // at once. hashMatched is true exactly when a video hash was sent.
    [[nodiscard]] concurrency::task<winrt::Windows::Data::Json::JsonObject> ResolveSubtitlesAsync(
        std::vector<ResolvableAddon> addons,
        std::wstring type,
        std::wstring videoId,
        SubtitleQuery query,
        ResolveContext context);

    struct NextEpisodeQuery final
    {
        std::wstring Type;
        std::wstring MetaId;
        std::wstring VideoId;
        // The addon that served the current stream.
        std::optional<std::wstring> AddonId;
        // The current stream's binge group.
        std::optional<std::wstring> BingeGroup;
    };

    // `{ video, stream }`: the episode after VideoId, and the stream of the
    // same addon with exactly the same binge group when that addon is still
    // installed (null otherwise, which means "fall back to the picker").
    // Empty when no addon can describe the title.
    [[nodiscard]] concurrency::task<std::optional<winrt::Windows::Data::Json::JsonObject>> ResolveNextEpisodeAsync(
        std::vector<ResolvableAddon> addons,
        NextEpisodeQuery query,
        ResolveContext context);

    struct ManifestFetchResult final
    {
        // In the order asked, when every manifest loaded.
        std::vector<std::pair<std::wstring, winrt::Windows::Data::Json::JsonObject>> Entries;
        // "could not fetch a valid manifest for <url>", for the first URL (in
        // the order asked) whose manifest failed.
        std::optional<std::wstring> Error;
    };

    // Every manifest at once, all or nothing.
    [[nodiscard]] concurrency::task<ManifestFetchResult> FetchManifestsAsync(
        std::vector<std::wstring> transportUrls,
        std::shared_ptr<IAddonFetcher> fetcher,
        std::chrono::milliseconds timeout);
}
