#pragma once

#include "Api/Addons/AddonClientPolicy.h"
#include "Api/Addons/AddonFetcher.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <winrt/Windows.Data.Json.h>

namespace HaloDesktop::Api::Addons
{
    // Addon answers are untrusted third-party JSON. These are the reference's
    // rules (`addon/manifest.ts`, `addon/normalize.ts` and the meta helpers
    // of `addon/resolve.ts`), which the server applies before it answers: only
    // the fields Halo uses survive, each bounded in length and type, and only
    // http(s) URLs. What they produce is the JSON the server would send, so
    // the app's own mappers read it next, exactly as they read the server's.
    // No I/O; safe on any thread.

    // The manifest, when its load-bearing fields have the right shape (a
    // non-empty id and name, a string version, string types, resources as
    // names or objects with a string name, catalogs with a string type and
    // id). Everything else passes through untouched.
    [[nodiscard]] std::optional<winrt::Windows::Data::Json::JsonObject> ParseManifest(
        winrt::Windows::Data::Json::IJsonValue const& value);

    // What decides which addon is asked for what. A prefix list that is not an
    // array matches nothing (the reference would fail the whole request).
    [[nodiscard]] ManifestFacts ReadManifestFacts(winrt::Windows::Data::Json::JsonObject const& manifest);

    // The manifest's `name` as sent, when it is a string.
    [[nodiscard]] std::optional<std::wstring> ManifestName(winrt::Windows::Data::Json::JsonObject const& manifest);

    // safeAddonName: control characters and runs of white space become one
    // space, and a name that looks like a URL, a path or a token reads "Addon".
    [[nodiscard]] std::wstring SafeAddonName(std::optional<std::wstring_view> name);

    struct ClassifiedFailure final
    {
        std::wstring Code;
        std::wstring Message;
        std::optional<std::uint16_t> Status;
    };

    [[nodiscard]] ClassifiedFailure ClassifyAddonFailure(AddonFailure const& failure);

    // One `errors` entry of a streams or subtitles answer.
    [[nodiscard]] winrt::Windows::Data::Json::JsonObject AddonErrorJson(
        std::wstring_view addonId,
        std::optional<std::wstring_view> addonName,
        AddonFailure const& failure);

    // The playable streams of a `{ streams: [...] }` answer (at most 500
    // considered), without the ones that would act instead of play. Throws
    // AddonRequestFailure (InvalidResponse) for any other shape.
    [[nodiscard]] winrt::Windows::Data::Json::JsonArray NormalizeStreamsResponse(
        winrt::Windows::Data::Json::IJsonValue const& value);

    // The usable subtitles of a `{ subtitles: [...] }` answer. Throws
    // AddonRequestFailure (InvalidResponse) for any other shape.
    [[nodiscard]] winrt::Windows::Data::Json::JsonArray NormalizeSubtitlesResponse(
        winrt::Windows::Data::Json::IJsonValue const& value);

    // A cast from an addon: at most 40 people, names and parts as bounded
    // text, photos only as https URLs. Empty unless someone is pictured.
    [[nodiscard]] std::optional<winrt::Windows::Data::Json::JsonArray> SanitizeCast(
        winrt::Windows::Data::Json::IJsonValue const& value);

    // Whether an addon's meta answer names a title (`meta.id` a non-empty
    // string). Addons answer 200 with `{ meta: null }`, `{ meta: {} }` or `{}`
    // for ids they do not know.
    [[nodiscard]] bool DescribesTitle(winrt::Windows::Data::Json::IJsonValue const& value);

    [[nodiscard]] bool HasPicturedCast(winrt::Windows::Data::Json::JsonObject const& meta);

    // The order keys of a meta's `videos`, for NextVideoIndex.
    [[nodiscard]] std::vector<VideoOrderKey> VideoOrderKeys(winrt::Windows::Data::Json::JsonArray const& videos);

    // A catalog answer's `metas` without the entries that are actions, not
    // titles. Empty when `metas` is not a list or holds a null, which the
    // reference fails on.
    [[nodiscard]] std::optional<winrt::Windows::Data::Json::JsonArray> WithoutActionEntries(
        winrt::Windows::Data::Json::IJsonValue const& metas);
}
