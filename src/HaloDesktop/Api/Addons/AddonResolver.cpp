#include "pch.h"
#include "Api/Addons/AddonResolver.h"

#include "Api/Addons/AddonNormalize.h"

#include <algorithm>
#include <exception>
#include <iterator>
#include <stdexcept>

namespace
{
    namespace Json = winrt::Windows::Data::Json;
    namespace Addons = HaloDesktop::Api::Addons;

    // The addons whose answers carry a pictured cast for an IMDb id, by
    // manifest id; the TMDB addon's manifest only claims its own `tmdb:` ids,
    // so resolution would never otherwise ask it.
    constexpr wchar_t PicturedCastAddon[] = L"tmdb-addon";

    [[noreturn]] void Fail(Addons::AddonFailureKind kind, std::optional<std::uint16_t> status = std::nullopt)
    {
        throw Addons::AddonRequestFailure{ { kind, status } };
    }

    std::int64_t NowMs(Addons::ResolveContext const& context)
    {
        if (context.Now) return context.Now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    void ThrowIfCancelled(Addons::ResolveContext const& context)
    {
        if (context.Cancellation.is_canceled()) throw winrt::hresult_canceled{};
    }

    std::wstring RequireUrl(std::optional<std::wstring> url)
    {
        // Only a lone surrogate in a type or id makes no URL, which
        // encodeURIComponent refuses with an error of no special kind.
        if (!url) Fail(Addons::AddonFailureKind::Unavailable);
        return std::move(*url);
    }

    // getJson: an answer outside 2xx is the addon's HTTP failure; a body that
    // is not JSON is an invalid response.
    concurrency::task<Json::IJsonValue> GetJsonAsync(std::wstring url, Addons::ResolveContext context)
    {
        if (!context.Fetcher) throw std::invalid_argument{ "Addon resolution requires a fetcher." };
        auto const response = co_await context.Fetcher->GetAsync(std::move(url), context.Timeout, context.Cancellation);
        if (response.Status < 200 || response.Status > 299) Fail(Addons::AddonFailureKind::UpstreamHttp, response.Status);
        Json::JsonValue parsed{ nullptr };
        if (!Json::JsonValue::TryParse(response.Body, parsed) || !parsed) Fail(Addons::AddonFailureKind::InvalidResponse);
        co_return parsed;
    }

    Json::JsonObject AddonJson(Addons::ResolvableAddon const& addon)
    {
        auto const name = Addons::ManifestName(addon.Manifest);
        Json::JsonObject result;
        result.Insert(L"id", Json::JsonValue::CreateStringValue(addon.Id));
        result.Insert(L"name", Json::JsonValue::CreateStringValue(Addons::SafeAddonName(
            name ? std::optional<std::wstring_view>{ *name } : std::nullopt)));
        return result;
    }

    Json::JsonObject ErrorJson(Addons::ResolvableAddon const& addon, Addons::AddonFailure const& failure)
    {
        auto const name = Addons::ManifestName(addon.Manifest);
        return Addons::AddonErrorJson(addon.Id, name ? std::optional<std::wstring_view>{ *name } : std::nullopt, failure);
    }

    // One addon's part of a fan-out: its list, or why it failed. Never throws,
    // so a fan-out can await every part whatever happens to the others.
    struct AddonOutcome final
    {
        Json::JsonArray Items{ nullptr };
        Addons::AddonFailure Failure;
    };

    Addons::AddonFailure FailureOf(std::exception_ptr const& error)
    {
        try
        {
            std::rethrow_exception(error);
        }
        catch (Addons::AddonRequestFailure const& failure)
        {
            return failure.Failure();
        }
        catch (winrt::hresult_canceled const&)
        {
            // The reference classifies an aborted request as a timeout too.
            return { Addons::AddonFailureKind::Timeout, std::nullopt };
        }
        catch (...)
        {
            return { Addons::AddonFailureKind::Unavailable, std::nullopt };
        }
    }

    concurrency::task<AddonOutcome> AskStreamsAsync(
        Addons::ResolvableAddon addon,
        std::wstring type,
        std::wstring videoId,
        Addons::ResolveContext context)
    {
        std::exception_ptr error;
        try
        {
            auto const url = RequireUrl(Addons::ResourceUrl(addon.TransportUrl, Addons::AddonResource::Stream, type, videoId));
            auto const answer = co_await GetJsonAsync(url, context);
            co_return AddonOutcome{ Addons::NormalizeStreamsResponse(answer), {} };
        }
        catch (...)
        {
            error = std::current_exception();
        }
        co_return AddonOutcome{ nullptr, FailureOf(error) };
    }

    concurrency::task<AddonOutcome> AskSubtitlesAsync(
        Addons::ResolvableAddon addon,
        std::wstring type,
        std::wstring videoId,
        Addons::ExtraArgs extra,
        Addons::ResolveContext context)
    {
        std::exception_ptr error;
        try
        {
            auto const url = RequireUrl(Addons::ResourceUrl(
                addon.TransportUrl, Addons::AddonResource::Subtitles, type, videoId, extra));
            auto const answer = co_await GetJsonAsync(url, context);
            co_return AddonOutcome{ Addons::NormalizeSubtitlesResponse(answer), {} };
        }
        catch (...)
        {
            error = std::current_exception();
        }
        co_return AddonOutcome{ nullptr, FailureOf(error) };
    }

    // Best effort: a failed or missing cast is simply none. Never throws.
    concurrency::task<std::optional<Json::JsonArray>> FetchPicturedCastAsync(
        Addons::ResolvableAddon addon,
        std::wstring type,
        std::wstring id,
        Addons::ResolveContext context)
    {
        try
        {
            auto const url = RequireUrl(Addons::ResourceUrl(addon.TransportUrl, Addons::AddonResource::Meta, type, id));
            auto const answer = co_await GetJsonAsync(url, context);
            if (!answer || answer.ValueType() != Json::JsonValueType::Object) co_return std::nullopt;
            auto const root = answer.GetObject();
            if (!root.HasKey(L"meta")) co_return std::nullopt;
            auto const meta = root.GetNamedValue(L"meta");
            if (!meta || meta.ValueType() != Json::JsonValueType::Object) co_return std::nullopt;
            auto const metaObject = meta.GetObject();
            if (!metaObject.HasKey(L"app_extras")) co_return std::nullopt;
            auto const extras = metaObject.GetNamedValue(L"app_extras");
            if (!extras || extras.ValueType() != Json::JsonValueType::Object) co_return std::nullopt;
            auto const extrasObject = extras.GetObject();
            if (!extrasObject.HasKey(L"cast")) co_return std::nullopt;
            co_return Addons::SanitizeCast(extrasObject.GetNamedValue(L"cast"));
        }
        catch (...)
        {
        }
        co_return std::nullopt;
    }
}

namespace HaloDesktop::Api::Addons
{
    ResolvableAddon MakeResolvableAddon(
        std::wstring id,
        std::wstring transportUrl,
        Json::JsonObject manifest)
    {
        if (!manifest) throw std::invalid_argument{ "A resolvable addon needs its manifest." };
        auto facts = ReadManifestFacts(manifest);
        return { std::move(id), std::move(transportUrl), std::move(manifest), std::move(facts) };
    }

    concurrency::task<Json::JsonObject> FetchCatalogAsync(
        ResolvableAddon addon,
        std::wstring type,
        std::wstring id,
        ExtraArgs extra,
        ResolveContext context)
    {
        auto const url = RequireUrl(ResourceUrl(addon.TransportUrl, AddonResource::Catalog, type, id, extra));
        auto const answer = co_await GetJsonAsync(url, context);
        if (!answer || answer.ValueType() != Json::JsonValueType::Object) Fail(AddonFailureKind::InvalidResponse);
        auto const response = answer.GetObject();
        auto const metas = WithoutActionEntries(
            response.HasKey(L"metas") ? response.GetNamedValue(L"metas") : Json::IJsonValue{ nullptr });
        if (!metas) Fail(AddonFailureKind::InvalidResponse);
        response.SetNamedValue(L"metas", *metas);
        co_return response;
    }

    concurrency::task<std::optional<Json::JsonObject>> ResolveMetaAsync(
        std::vector<ResolvableAddon> addons,
        std::wstring type,
        std::wstring id,
        ResolveContext context)
    {
        for (auto const& addon : addons)
        {
            if (!AddonSupportsResource(addon.Facts, AddonResource::Meta, type, id)) continue;
            ThrowIfCancelled(context);
            try
            {
                auto const url = RequireUrl(ResourceUrl(addon.TransportUrl, AddonResource::Meta, type, id));
                auto const answer = co_await GetJsonAsync(url, context);
                if (DescribesTitle(answer)) co_return answer.GetObject();
            }
            catch (winrt::hresult_canceled const&)
            {
                throw;
            }
            catch (...)
            {
                // Try the next addon that can describe this id.
            }
        }
        co_return std::nullopt;
    }

    concurrency::task<std::optional<Json::JsonObject>> ResolveMetaWithCastAsync(
        std::vector<ResolvableAddon> addons,
        std::wstring type,
        std::wstring id,
        ResolveContext context)
    {
        std::optional<ResolvableAddon> castSource;
        if (IsImdbId(id))
        {
            auto const found = std::find_if(addons.begin(), addons.end(), [&type](ResolvableAddon const& addon)
            {
                return addon.Facts.Id == PicturedCastAddon
                    && AddonSupportsResource(addon.Facts, AddonResource::Meta, type);
            });
            if (found != addons.end()) castSource = *found;
        }

        // Both start now; the cast request never throws, so it is awaited
        // whatever the meta request does.
        auto metaTask = ResolveMetaAsync(addons, type, id, context);
        std::optional<concurrency::task<std::optional<Json::JsonArray>>> castTask;
        if (castSource) castTask = FetchPicturedCastAsync(*castSource, type, id, context);

        std::optional<Json::JsonObject> answer;
        std::exception_ptr failure;
        try
        {
            answer = co_await metaTask;
        }
        catch (...)
        {
            failure = std::current_exception();
        }
        std::optional<Json::JsonArray> cast;
        if (castTask) cast = co_await *castTask;
        if (failure) std::rethrow_exception(failure);

        if (!answer || !cast) co_return answer;
        auto const meta = answer->GetNamedObject(L"meta");
        if (HasPicturedCast(meta)) co_return answer;

        // { ...meta.app_extras, cast }
        Json::JsonObject extras;
        if (meta.HasKey(L"app_extras"))
        {
            auto const existing = meta.GetNamedValue(L"app_extras");
            if (existing && existing.ValueType() == Json::JsonValueType::Object)
            {
                for (auto const& entry : existing.GetObject()) extras.Insert(entry.Key(), entry.Value());
            }
        }
        extras.SetNamedValue(L"cast", *cast);
        meta.SetNamedValue(L"app_extras", extras);
        co_return answer;
    }

    concurrency::task<Json::JsonObject> ResolveStreamsAsync(
        std::vector<ResolvableAddon> addons,
        std::wstring type,
        std::wstring videoId,
        ResolveContext context)
    {
        std::vector<ResolvableAddon> capable;
        std::copy_if(addons.begin(), addons.end(), std::back_inserter(capable), [&type, &videoId](ResolvableAddon const& addon)
        {
            return AddonSupportsResource(addon.Facts, AddonResource::Stream, type, videoId);
        });
        std::vector<concurrency::task<AddonOutcome>> asks;
        asks.reserve(capable.size());
        for (auto const& addon : capable) asks.push_back(AskStreamsAsync(addon, type, videoId, context));

        Json::JsonArray results;
        Json::JsonArray errors;
        for (std::size_t index = 0; index < capable.size(); ++index)
        {
            auto const outcome = co_await asks[index];
            if (outcome.Items)
            {
                if (outcome.Items.Size() == 0) continue;
                Json::JsonObject group;
                group.Insert(L"addon", AddonJson(capable[index]));
                group.Insert(L"streams", outcome.Items);
                results.Append(group);
                continue;
            }
            errors.Append(ErrorJson(capable[index], outcome.Failure));
        }
        ThrowIfCancelled(context);
        Json::JsonObject result;
        result.Insert(L"results", results);
        result.Insert(L"errors", errors);
        co_return result;
    }

    concurrency::task<Json::JsonObject> ResolveSubtitlesAsync(
        std::vector<ResolvableAddon> addons,
        std::wstring type,
        std::wstring videoId,
        SubtitleQuery query,
        ResolveContext context)
    {
        std::vector<ResolvableAddon> capable;
        std::copy_if(addons.begin(), addons.end(), std::back_inserter(capable), [&type, &videoId](ResolvableAddon const& addon)
        {
            return AddonSupportsResource(addon.Facts, AddonResource::Subtitles, type, videoId);
        });
        auto const extra = SubtitleExtras(query);
        std::vector<concurrency::task<AddonOutcome>> asks;
        asks.reserve(capable.size());
        for (auto const& addon : capable) asks.push_back(AskSubtitlesAsync(addon, type, videoId, extra, context));

        Json::JsonArray results;
        Json::JsonArray errors;
        for (std::size_t index = 0; index < capable.size(); ++index)
        {
            auto const outcome = co_await asks[index];
            if (outcome.Items)
            {
                Json::JsonObject group;
                group.Insert(L"addon", AddonJson(capable[index]));
                group.Insert(L"subtitles", outcome.Items);
                results.Append(group);
                continue;
            }
            errors.Append(ErrorJson(capable[index], outcome.Failure));
        }
        ThrowIfCancelled(context);
        Json::JsonObject result;
        result.Insert(L"results", results);
        result.Insert(L"errors", errors);
        result.Insert(L"hashMatched", Json::JsonValue::CreateBooleanValue(query.VideoHash.has_value()));
        co_return result;
    }

    concurrency::task<std::optional<Json::JsonObject>> ResolveNextEpisodeAsync(
        std::vector<ResolvableAddon> addons,
        NextEpisodeQuery query,
        ResolveContext context)
    {
        auto const answer = co_await ResolveMetaAsync(addons, query.Type, query.MetaId, context);
        if (!answer) co_return std::nullopt;

        Json::JsonObject result;
        result.Insert(L"video", Json::JsonValue::CreateNullValue());
        result.Insert(L"stream", Json::JsonValue::CreateNullValue());

        auto const meta = answer->GetNamedObject(L"meta");
        Json::JsonArray videos;
        if (meta.HasKey(L"videos"))
        {
            auto const value = meta.GetNamedValue(L"videos");
            if (value && value.ValueType() == Json::JsonValueType::Array) videos = value.GetArray();
        }
        auto const keys = VideoOrderKeys(videos);
        auto const nextIndex = NextVideoIndex(keys, query.VideoId, NowMs(context));
        if (!nextIndex) co_return result;
        auto const next = videos.GetAt(static_cast<std::uint32_t>(*nextIndex));
        result.SetNamedValue(L"video", next);

        // A missing addon id is benign: the addon was uninstalled mid-playback,
        // and the next episode is still reported, just without a stream.
        auto const& nextId = keys[*nextIndex].Id;
        auto const entry = query.AddonId
            ? std::find_if(addons.begin(), addons.end(), [&query](ResolvableAddon const& addon) { return addon.Id == *query.AddonId; })
            : addons.end();
        if (entry != addons.end() && query.BingeGroup && !query.BingeGroup->empty() && nextId
            && AddonSupportsResource(entry->Facts, AddonResource::Stream, query.Type, *nextId))
        {
            try
            {
                auto const url = RequireUrl(ResourceUrl(entry->TransportUrl, AddonResource::Stream, query.Type, *nextId));
                auto const streams = NormalizeStreamsResponse(co_await GetJsonAsync(url, context));
                for (auto const& stream : streams)
                {
                    auto const object = stream.GetObject();
                    if (!object.HasKey(L"behaviorHints")) continue;
                    auto const hints = object.GetNamedObject(L"behaviorHints");
                    if (hints.HasKey(L"bingeGroup") && hints.GetNamedString(L"bingeGroup") == *query.BingeGroup)
                    {
                        result.SetNamedValue(L"stream", object);
                        break;
                    }
                }
            }
            catch (...)
            {
                // Best effort: an unreachable addon degrades to the picker.
            }
        }
        ThrowIfCancelled(context);
        co_return result;
    }

    concurrency::task<ManifestFetchResult> FetchManifestsAsync(
        std::vector<std::wstring> transportUrls,
        std::shared_ptr<IAddonFetcher> fetcher,
        std::chrono::milliseconds timeout)
    {
        auto const fetchOne = [](std::wstring transportUrl, std::shared_ptr<IAddonFetcher> source, std::chrono::milliseconds budget)
            -> concurrency::task<std::optional<Json::JsonObject>>
        {
            try
            {
                ResolveContext context;
                context.Fetcher = std::move(source);
                context.Timeout = budget;
                auto const answer = co_await GetJsonAsync(ManifestUrl(transportUrl), context);
                co_return ParseManifest(answer);
            }
            catch (...)
            {
            }
            co_return std::nullopt;
        };

        std::vector<concurrency::task<std::optional<Json::JsonObject>>> fetches;
        fetches.reserve(transportUrls.size());
        for (auto const& url : transportUrls) fetches.push_back(fetchOne(url, fetcher, timeout));

        ManifestFetchResult result;
        for (std::size_t index = 0; index < transportUrls.size(); ++index)
        {
            auto const manifest = co_await fetches[index];
            if (!manifest)
            {
                if (!result.Error) result.Error = L"could not fetch a valid manifest for " + transportUrls[index];
                continue;
            }
            result.Entries.emplace_back(transportUrls[index], *manifest);
        }
        if (result.Error) result.Entries.clear();
        co_return result;
    }
}
