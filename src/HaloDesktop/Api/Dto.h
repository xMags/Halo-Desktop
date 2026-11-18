#pragma once

#include <chrono>
#include <optional>
#include <vector>
#include <winrt/Windows.Data.Json.h>
#include <winrt/base.h>

namespace HaloDesktop::Api::Dto
{
    enum class AuthMode
    {
        Local,
        Oidc,
    };

    struct AuthConfig final
    {
        AuthMode Mode{ AuthMode::Local };
        winrt::hstring Issuer;
        winrt::hstring ClientId;
        std::vector<winrt::hstring> Scopes;
    };

    struct HealthStatus final
    {
        bool Ok{};
        std::chrono::milliseconds RoundTrip{};
    };

    struct IssuedToken final
    {
        winrt::hstring Token;
        std::int64_t ExpiresAt{};
    };

    struct Me final
    {
        winrt::hstring Id;
        winrt::hstring Username;
        bool IsAdmin{};
        std::int64_t CreatedAt{};
        // The account's tier from a server that has tiers (`premium`).
        std::optional<winrt::hstring> Plan;
        // Fingerprint of the profile picture behind GET /auth/avatar; it
        // changes exactly when the picture does, so the image is cached by it.
        std::optional<winrt::hstring> AvatarVersion;
    };

    struct AddonRecord final
    {
        struct CatalogExtra final
        {
            winrt::hstring Name;
            bool IsRequired{};
            std::vector<winrt::hstring> Options;
        };

        struct Catalog final
        {
            winrt::hstring Type;
            winrt::hstring Id;
            std::optional<winrt::hstring> Name;
            bool SupportsSearch{};
            bool HasRequiredExtra{};
            std::vector<CatalogExtra> Extras;
            // The legacy alternatives to `extra` some addons still emit.
            std::vector<winrt::hstring> ExtraSupported;
            std::vector<winrt::hstring> ExtraRequired;
        };

        winrt::hstring Id;
        // The addon's own declared identity, shared by the global and user rows
        // that name the same addon. Empty when the manifest omitted one.
        winrt::hstring ManifestId;
        std::optional<winrt::hstring> TransportUrl;
        winrt::hstring Name;
        winrt::hstring Version;
        // The manifest's own description; Settings shows it for an addon that
        // declares no catalogs or resources. Empty when it gave none.
        winrt::hstring Description;
        std::vector<winrt::hstring> Resources;
        std::vector<winrt::hstring> Types;
        std::vector<Catalog> Catalogs;
        std::int32_t Position{};
        bool HideCatalogs{};
        bool IsGlobal{};
        // Personal entries only: the admin installs this same transport URL for
        // everyone, so the server answers from the global entry instead.
        bool ProvidedGlobally{};
    };

    struct AddonsPayload final
    {
        std::vector<AddonRecord> Global;
        std::vector<AddonRecord> User;
    };

    struct SettingsPayload final
    {
        winrt::Windows::Data::Json::JsonObject Value;
        std::int64_t UpdatedAt{};
    };

    struct MetaPreview final
    {
        winrt::hstring Id;
        winrt::hstring Type;
        winrt::hstring Name;
        std::optional<winrt::hstring> Poster;
        std::optional<winrt::hstring> Background;
        std::optional<winrt::hstring> Description;
        std::optional<winrt::hstring> ReleaseInfo;
        // The addon's raw `imdbRating`; RatedValue decides whether it is one.
        std::optional<winrt::hstring> Rating;
        std::optional<winrt::hstring> Logo;
        // `behaviorHints.defaultVideoId`: the video a film plays as when that
        // differs from its id (the TMDB addon lists films as `tmdb:` ids but
        // plays them by IMDb id).
        std::optional<winrt::hstring> DefaultVideoId;
        std::vector<winrt::hstring> Genres;
        // `imdb_id`, sent beside an addon's own id; kept only in IMDb shape.
        std::optional<winrt::hstring> ImdbId;
    };

    struct MetaVideo final
    {
        winrt::hstring Id;
        winrt::hstring Title;
        std::optional<winrt::hstring> Released;
        std::optional<winrt::hstring> Thumbnail;
        std::optional<winrt::hstring> Overview;
        std::optional<std::int32_t> Season;
        std::optional<std::int32_t> Episode;
        // False when the addon sent neither `title` nor `name`; Title is then
        // empty and each reader picks its own fallback.
        bool HasTitle{};
    };

    struct MetaPerson final
    {
        winrt::hstring Name;
        std::optional<winrt::hstring> Character;
        std::optional<winrt::hstring> Photo;
    };

    struct MetaTrailer final
    {
        winrt::hstring Source;
        std::optional<winrt::hstring> Type;
    };

    struct MetaDetail final
    {
        MetaPreview Preview;
        std::vector<MetaVideo> Videos;
        std::optional<winrt::hstring> Runtime;
        std::vector<winrt::hstring> Cast;
        std::vector<winrt::hstring> Director;
        std::vector<winrt::hstring> Writer;
        // ISO date of the first release, finer than the preview's releaseInfo.
        std::optional<winrt::hstring> Released;
        // A local certificate such as `U/A 13+`.
        std::optional<winrt::hstring> AgeRating;
        // `trailerStreams[].ytId`, in the addon's order.
        std::vector<winrt::hstring> TrailerStreamIds;
        // The older trailer list, `{ source, type }`.
        std::vector<MetaTrailer> Trailers;
        // `app_extras.cast`: the cast with parts and photos some addons send.
        std::vector<MetaPerson> PicturedCast;
        std::optional<winrt::hstring> Country;
        std::optional<winrt::hstring> Language;
        std::optional<winrt::hstring> Awards;
    };

    struct LibraryRow final
    {
        winrt::hstring Id;
        winrt::hstring Type;
        winrt::hstring Name;
        std::optional<winrt::hstring> Poster;
        std::int64_t AddedAt{};
        std::optional<std::int64_t> RemovedAt;
        std::int64_t UpdatedAt{};
    };

    struct WatchEntry final
    {
        winrt::hstring VideoId;
        winrt::hstring ItemId;
        double PositionSec{};
        double DurationSec{};
        bool Watched{};
        std::optional<winrt::hstring> Name;
        std::optional<winrt::hstring> Poster;
        std::int64_t UpdatedAt{};
        // Set on a removal from the watch history: the row is a tombstone that
        // forgets its progress and wins over stale copies. Servers never return
        // tombstones; a newer row without it brings the video back.
        std::optional<std::int64_t> RemovedAt;
    };

    struct StreamSubtitleRecord final
    {
        winrt::hstring Id;
        winrt::hstring Url;
        winrt::hstring Lang;
    };

    struct StreamRecord final
    {
        winrt::hstring Url;
        std::optional<winrt::hstring> Name,Title,Description,Filename,BingeGroup,VideoHash;
        std::optional<std::uint64_t> VideoSize;
        std::vector<std::pair<winrt::hstring,winrt::hstring>> RequestHeaders;
        std::vector<StreamSubtitleRecord> Subtitles;
    };
    struct StreamGroup final{winrt::hstring AddonId,AddonName;std::vector<StreamRecord> Streams;};
    struct AddonFailure final{std::optional<winrt::hstring>Name,Code;};
    struct StreamsPayload final{std::vector<StreamGroup>Results;std::vector<AddonFailure>Errors;};
    struct NextEpisodePayload final
    {
        std::optional<MetaVideo> Video;
        std::optional<StreamRecord> Stream;
    };
    struct SubtitleRecord final{winrt::hstring Id,Url,Lang;};
    struct SubtitleGroup final{winrt::hstring AddonId,AddonName;std::vector<SubtitleRecord>Subtitles;};
    struct SubtitlesPayload final{std::vector<SubtitleGroup>Results;std::vector<AddonFailure>Errors;bool HashMatched{};};
}

namespace HaloDesktop::Api::Mappers
{
    [[nodiscard]] bool ParseHealth(winrt::Windows::Data::Json::IJsonValue const& value);
    [[nodiscard]] Dto::AuthConfig ParseAuthConfig(winrt::Windows::Data::Json::IJsonValue const& value);
    [[nodiscard]] Dto::IssuedToken ParseIssuedToken(winrt::Windows::Data::Json::IJsonValue const& value);
    [[nodiscard]] Dto::Me ParseMe(winrt::Windows::Data::Json::IJsonValue const& value);
    [[nodiscard]] Dto::AddonsPayload ParseAddons(winrt::Windows::Data::Json::IJsonValue const& value);
    [[nodiscard]] Dto::SettingsPayload ParseSettings(winrt::Windows::Data::Json::IJsonValue const& value);
    [[nodiscard]] std::vector<Dto::MetaPreview> ParseCatalog(winrt::Windows::Data::Json::IJsonValue const& value);
    [[nodiscard]] std::vector<Dto::LibraryRow> ParseLibrary(winrt::Windows::Data::Json::IJsonValue const& value);
    [[nodiscard]] std::vector<Dto::WatchEntry> ParseWatchState(winrt::Windows::Data::Json::IJsonValue const& value);
    [[nodiscard]] Dto::MetaDetail ParseMeta(winrt::Windows::Data::Json::IJsonValue const& value);
    [[nodiscard]] Dto::StreamsPayload ParseStreams(winrt::Windows::Data::Json::IJsonValue const& value);
    [[nodiscard]] Dto::NextEpisodePayload ParseNextEpisode(winrt::Windows::Data::Json::IJsonValue const& value);
    [[nodiscard]] Dto::SubtitlesPayload ParseSubtitles(winrt::Windows::Data::Json::IJsonValue const& value);

    // The server's sentence when an account's plan refuses something
    // (`{ code: "plan_required" | "plan_limit_reached", error }`), sanitised
    // like every other server message; empty for any other body.
    [[nodiscard]] std::optional<winrt::hstring> PlanRefusalMessage(winrt::hstring const& body);
}
