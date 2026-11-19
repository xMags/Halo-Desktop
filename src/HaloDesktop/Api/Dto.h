#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>
#include <winrt/Windows.Data.Json.h>
#include <winrt/base.h>

namespace HaloDesktop::Api::Dto
{
    enum class AuthMode
    {
        Local,
        Oidc,
        // A mode this app does not know itself; only a sign-in add-on for
        // ModeName can read it.
        Other,
    };

    struct AuthConfig final
    {
        AuthMode Mode{ AuthMode::Local };
        winrt::hstring Issuer;
        winrt::hstring ClientId;
        std::vector<winrt::hstring> Scopes;
        // The server's `mode` as sent: "local", "oidc", or any other name.
        winrt::hstring ModeName;
        // An Other mode's whole answer, untrusted: the add-on validates what
        // it reads. Null for the built-in modes.
        winrt::Windows::Data::Json::JsonObject Raw{ nullptr };
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
        // The person's TMDB id, when the cast entry carries a valid one.
        std::optional<std::int64_t> TmdbId;
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

    // What a server can do beyond the shared Halo contract
    // (`GET /capabilities`). All false for a server that does not answer it.
    struct Capabilities final
    {
        // People search and person pages.
        bool People{};
        // Pasted TVDB numbers and IMDb ids looked up through the server.
        bool Lookup{};

        bool operator==(Capabilities const&) const = default;
    };

    // The department a person is best known for, as the server names it.
    enum class PersonDepartment
    {
        Unknown,
        Acting,
        Directing,
        Writing,
    };

    // What a person did on one title.
    enum class CreditRole
    {
        Cast,
        Director,
        Writer,
    };

    // One answer of the people search.
    struct PersonSummary final
    {
        std::int64_t TmdbId{};
        winrt::hstring Name;
        PersonDepartment KnownFor{ PersonDepartment::Unknown };
        // Only on https image.tmdb.org.
        std::optional<winrt::hstring> Photo;
        double Popularity{};
        // At most three, most popular first.
        std::vector<winrt::hstring> KnownForTitles;

        bool operator==(PersonSummary const&) const = default;
    };

    // One title of a person's filmography, already mapped to its IMDb id.
    struct PersonCredit final
    {
        // `movie` or `series`.
        winrt::hstring Type;
        winrt::hstring ImdbId;
        std::int64_t TmdbId{};
        winrt::hstring Name;
        // Four digits.
        std::optional<winrt::hstring> Year;
        // Only on https image.tmdb.org.
        std::optional<winrt::hstring> Poster;
        // Each role at most once, never empty.
        std::vector<CreditRole> Roles;
        std::optional<winrt::hstring> Character;
        double Popularity{};

        bool operator==(PersonCredit const&) const = default;
    };

    struct PersonProfile final
    {
        std::int64_t TmdbId{};
        winrt::hstring Name;
        std::optional<winrt::hstring> Photo;
        PersonDepartment KnownFor{ PersonDepartment::Unknown };
        std::optional<winrt::hstring> Biography;
    };

    struct PersonDetail final
    {
        PersonProfile Person;
        std::vector<PersonCredit> Credits;
        // The server ran out of time mapping credits, so some may be missing.
        bool Partial{};
    };

    enum class IdLookupKind
    {
        Title,
        Person,
    };

    // What a pasted TVDB number or IMDb id names (`GET /lookup`).
    struct IdLookup final
    {
        IdLookupKind Kind{ IdLookupKind::Title };
        winrt::hstring Name;
        // A title: `movie` or `series`, and its IMDb id.
        winrt::hstring Type;
        winrt::hstring Id;
        std::optional<winrt::hstring> Year;
        std::optional<winrt::hstring> Poster;
        // A person: their TMDB id and photo.
        std::int64_t TmdbId{};
        std::optional<winrt::hstring> Photo;
    };
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

    // Api/PeopleMappers.cpp. A field of the wrong type, out of bounds or of
    // the wrong shape is dropped, and so is an entry missing a required
    // field; only a body that is not the expected object throws.
    [[nodiscard]] Dto::Capabilities ParseCapabilities(winrt::Windows::Data::Json::IJsonValue const& value);
    [[nodiscard]] std::vector<Dto::PersonSummary> ParsePeopleSearch(winrt::Windows::Data::Json::IJsonValue const& value);
    [[nodiscard]] Dto::PersonDetail ParsePerson(winrt::Windows::Data::Json::IJsonValue const& value);
    [[nodiscard]] Dto::IdLookup ParseIdLookup(winrt::Windows::Data::Json::IJsonValue const& value);
    // An image URL the people answers may carry: https on image.tmdb.org,
    // under /t/p/, in plain path characters only.
    [[nodiscard]] bool IsTmdbImageUrl(std::wstring_view url) noexcept;

    // The server's sentence when an account's plan refuses something
    // (`{ code: "plan_required" | "plan_limit_reached", error }`), sanitised
    // like every other server message; empty for any other body.
    [[nodiscard]] std::optional<winrt::hstring> PlanRefusalMessage(winrt::hstring const& body);
}
