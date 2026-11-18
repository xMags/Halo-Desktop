#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace HaloDesktop::Api::Addons
{
    // Pure rules of the Stremio addon protocol as the reference client's
    // `addon/client.ts` and `addon/resolve.ts` apply them: URL building, extra
    // argument encoding, which addon serves what, and the request limits the
    // server enforces. No I/O.
    //
    //   {transport}/manifest.json
    //   {transport}/{resource}/{type}/{id}.json
    //   {transport}/{resource}/{type}/{id}/{extra}.json
    //
    // where {transport} is the manifest URL without /manifest.json and {extra}
    // is query-string encoded ("skip=100&genre=Action").

    // Per-addon request budget for every resolution call (RESOLVE_TIMEOUT_MS).
    inline constexpr std::chrono::milliseconds ResolveTimeout{ 10000 };
    inline constexpr std::size_t MaximumBingeGroupLength = 512;

    // JavaScript's String.prototype.trim and \s: the WhiteSpace and
    // LineTerminator code points of ECMAScript.
    [[nodiscard]] bool IsJsWhitespace(wchar_t character) noexcept;
    [[nodiscard]] std::wstring JsTrim(std::wstring_view text);

    // What the WHATWG URL parser (`new URL(value)`) would make of an absolute
    // URL, approximated closely enough for the reference's checks: a scheme
    // is required, and the special schemes (http, https, ws, wss, ftp) need a
    // non-empty host with no forbidden characters and a valid port.
    struct UrlFacts final
    {
        // Lower case, without the colon.
        std::wstring Scheme;
        // Lower case; empty for a scheme without one.
        std::wstring Host;
    };

    [[nodiscard]] std::optional<UrlFacts> ParseUrl(std::wstring_view value);
    // `new URL(value)` succeeds.
    [[nodiscard]] bool IsUrl(std::wstring_view value);
    // An http or https URL with a host.
    [[nodiscard]] bool IsHttpUrl(std::wstring_view value);
    [[nodiscard]] bool IsHttpsUrl(std::wstring_view value);

    // Strips a trailing /manifest.json, then one trailing slash.
    [[nodiscard]] std::wstring TransportBase(std::wstring_view transportUrl);
    [[nodiscard]] std::wstring ManifestUrl(std::wstring_view transportUrl);

    // encodeURIComponent over UTF-8. Empty when the text holds a lone
    // surrogate, which encodeURIComponent refuses.
    [[nodiscard]] std::optional<std::wstring> EncodeUriComponent(std::wstring_view text);

    using ExtraArgs = std::vector<std::pair<std::wstring, std::wstring>>;

    // `name=value` pairs joined with `&`, each half encoded.
    [[nodiscard]] std::optional<std::wstring> EncodeExtra(ExtraArgs const& extra);

    enum class AddonResource
    {
        Catalog,
        Meta,
        Stream,
        Subtitles,
    };

    [[nodiscard]] std::wstring_view ResourceName(AddonResource resource) noexcept;

    // The resource URL, with the extra segment only when there are extras.
    [[nodiscard]] std::optional<std::wstring> ResourceUrl(
        std::wstring_view transportUrl,
        AddonResource resource,
        std::wstring_view type,
        std::wstring_view id,
        ExtraArgs const& extra = {});

    // The parts of a manifest that decide which addon is asked for what.
    struct ManifestResourceEntry final
    {
        std::wstring Name;
        // A bare name, governed by the manifest's own types and prefixes.
        bool Bare{};
        std::vector<std::wstring> Types;
        std::optional<std::vector<std::wstring>> IdPrefixes;
    };

    struct ManifestFacts final
    {
        std::wstring Id;
        std::vector<std::wstring> Types;
        std::optional<std::vector<std::wstring>> IdPrefixes;
        std::vector<ManifestResourceEntry> Resources;
    };

    // Whether the addon serves the resource for this type and id. An empty id
    // matches any prefix list, as the reference's missing id does.
    [[nodiscard]] bool AddonSupportsResource(
        ManifestFacts const& manifest,
        AddonResource resource,
        std::wstring_view type,
        std::wstring_view id = {});

    // A catalog request's extras as the server reads its query string: the
    // addon, type and id parameters are not extras, and a repeated name keeps
    // its first value.
    [[nodiscard]] ExtraArgs CatalogExtras(ExtraArgs const& requested);
    [[nodiscard]] std::optional<std::wstring> CatalogExtraProblem(ExtraArgs const& extra);

    struct SubtitleQuery final
    {
        std::optional<std::wstring> VideoHash;
        std::optional<std::uint64_t> VideoSize;
        std::optional<std::wstring> Filename;
    };

    [[nodiscard]] std::optional<std::wstring> SubtitleQueryProblem(SubtitleQuery const& query);
    // The extras a subtitle request sends: each value only when it is set and
    // not empty or zero, in the order videoHash, videoSize, filename.
    [[nodiscard]] ExtraArgs SubtitleExtras(SubtitleQuery const& query);

    // The episode order key of one meta video, as `nextVideo` reads it.
    struct VideoOrderKey final
    {
        // Empty when the video's id is not a string, which never matches.
        std::optional<std::wstring> Id;
        std::optional<double> Season;
        std::optional<double> Episode;
        // `released`, when it is a date the app can read; an unreadable date
        // never hides a video, as `new Date(text)` giving NaN never does.
        std::optional<std::int64_t> ReleasedAtMs;
    };

    // The index of the video that follows `currentVideoId` in playback order
    // (seasons ascending with specials, season 0, last; episodes ascending), or
    // empty when it is unknown, the last one, or not released by `nowMs`.
    // Videos keep their order unless every one is numbered: the reference's
    // comparator is only an ordering when they all are.
    [[nodiscard]] std::optional<std::size_t> NextVideoIndex(
        std::vector<VideoOrderKey> const& videos,
        std::wstring_view currentVideoId,
        std::int64_t nowMs);

    // Milliseconds since the epoch for an ISO 8601 date or date-time (the
    // forms addons send); empty for anything else. A date-time without an
    // offset is read as UTC.
    [[nodiscard]] std::optional<std::int64_t> ParseIsoDateMs(std::wstring_view text);

    // `^tt\d{1,10}$`.
    [[nodiscard]] bool IsImdbId(std::wstring_view id) noexcept;
}
