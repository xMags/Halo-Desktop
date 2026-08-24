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
    };

    struct AddonRecord final
    {
        struct Catalog final
        {
            winrt::hstring Type;
            winrt::hstring Id;
            std::optional<winrt::hstring> Name;
            bool SupportsSearch{};
            bool HasRequiredExtra{};
        };

        winrt::hstring Id;
        std::optional<winrt::hstring> TransportUrl;
        winrt::hstring Name;
        winrt::hstring Version;
        std::vector<winrt::hstring> Resources;
        std::vector<winrt::hstring> Types;
        std::vector<Catalog> Catalogs;
        std::int32_t Position{};
        bool HideCatalogs{};
        bool IsGlobal{};
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
        std::optional<winrt::hstring> Rating;
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
    };

    struct MetaDetail final
    {
        MetaPreview Preview;
        std::vector<MetaVideo> Videos;
        std::optional<winrt::hstring> Runtime;
        std::vector<winrt::hstring> Genres;
        std::vector<winrt::hstring> Cast;
        std::vector<winrt::hstring> Director;
        std::vector<winrt::hstring> Writer;
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
    };

    struct StreamRecord final
    {
        winrt::hstring Url;
        std::optional<winrt::hstring> Name,Title,Description,Filename,BingeGroup,VideoHash;
        std::optional<std::uint64_t> VideoSize;
        std::vector<std::pair<winrt::hstring,winrt::hstring>> RequestHeaders;
    };
    struct StreamGroup final{winrt::hstring AddonId,AddonName;std::vector<StreamRecord> Streams;};
    struct AddonFailure final{std::optional<winrt::hstring>Name,Code;};
    struct StreamsPayload final{std::vector<StreamGroup>Results;std::vector<AddonFailure>Errors;};
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
    [[nodiscard]] Dto::SubtitlesPayload ParseSubtitles(winrt::Windows::Data::Json::IJsonValue const& value);
}
