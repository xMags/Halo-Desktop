#include "pch.h"
#include "Api/Dto.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
    winrt::Windows::Data::Json::JsonObject RequireObject(
        winrt::Windows::Data::Json::IJsonValue const& value,
        wchar_t const* description)
    {
        if (!value || value.ValueType() != winrt::Windows::Data::Json::JsonValueType::Object)
        {
            throw std::invalid_argument{ winrt::to_string(description) };
        }
        return value.GetObject();
    }

    winrt::hstring Trimmed(winrt::hstring const& value)
    {
        std::wstring text{ value };
        auto const first = std::find_if_not(text.begin(), text.end(), [](wchar_t character)
        {
            return std::iswspace(character) != 0;
        });
        auto const last = std::find_if_not(text.rbegin(), text.rend(), [](wchar_t character)
        {
            return std::iswspace(character) != 0;
        }).base();
        if (first >= last)
        {
            return L"";
        }
        return winrt::hstring{ std::wstring(first, last) };
    }

    winrt::hstring RequireString(
        winrt::Windows::Data::Json::JsonObject const& object,
        wchar_t const* key)
    {
        auto const value = Trimmed(object.GetNamedString(key));
        if (value.empty() || value.size() > 65536)
        {
            throw std::invalid_argument{ "The server returned an empty required field." };
        }
        return value;
    }

    winrt::hstring DisplayString(
        winrt::Windows::Data::Json::JsonObject const& object,
        wchar_t const* key,
        std::size_t maximumLength)
    {
        std::wstring value{ RequireString(object, key) };
        std::replace_if(value.begin(), value.end(), [](wchar_t character)
        {
            return std::iswcntrl(character) != 0;
        }, L' ');
        if (value.size() > maximumLength)
        {
            value.resize(maximumLength);
        }
        return winrt::hstring{ value };
    }

    std::int64_t RequirePositiveInteger(
        winrt::Windows::Data::Json::JsonObject const& object,
        wchar_t const* key)
    {
        auto const value = object.GetNamedNumber(key);
        if (!std::isfinite(value) || value <= 0 || std::floor(value) != value
            || value > static_cast<double>((std::numeric_limits<std::int64_t>::max)()))
        {
            throw std::invalid_argument{ "The server returned an invalid integer field." };
        }
        return static_cast<std::int64_t>(value);
    }

    std::vector<winrt::hstring> StringArray(
        winrt::Windows::Data::Json::JsonObject const& object,
        wchar_t const* key)
    {
        std::vector<winrt::hstring> result;
        auto const array = object.GetNamedArray(key, winrt::Windows::Data::Json::JsonArray{});
        result.reserve((std::min)(array.Size(), 128u));
        for (auto const& item : array)
        {
            if (result.size() == 128)
            {
                break;
            }
            if (item.ValueType() == winrt::Windows::Data::Json::JsonValueType::String)
            {
                auto const value = Trimmed(item.GetString());
                if (!value.empty() && value.size() <= 512)
                {
                    result.push_back(value);
                }
            }
            else if (item.ValueType() == winrt::Windows::Data::Json::JsonValueType::Object)
            {
                auto const name = Trimmed(item.GetObject().GetNamedString(L"name", L""));
                if (!name.empty() && name.size() <= 512)
                {
                    result.push_back(name);
                }
            }
        }
        return result;
    }

    std::optional<winrt::hstring> OptionalDisplayString(
        winrt::Windows::Data::Json::JsonObject const& object,
        wchar_t const* key,
        std::size_t maximumLength)
    {
        if (!object.HasKey(key)) return std::nullopt;
        auto const value = object.GetNamedValue(key);
        if (value.ValueType() != winrt::Windows::Data::Json::JsonValueType::String) return std::nullopt;
        std::wstring text{ value.GetString() };
        std::replace_if(text.begin(), text.end(), [](wchar_t character) { return std::iswcntrl(character) != 0; }, L' ');
        if (text.empty()) return std::nullopt;
        if (text.size() > maximumLength) text.resize(maximumLength);
        return winrt::hstring{ text };
    }

    std::optional<winrt::hstring> OptionalHttpUrl(
        winrt::Windows::Data::Json::JsonObject const& object,
        wchar_t const* key)
    {
        auto const value = OptionalDisplayString(object, key, 4096);
        if (!value) return std::nullopt;
        try
        {
            winrt::Windows::Foundation::Uri const uri{ *value };
            if (!uri.Host().empty() && (uri.SchemeName() == L"http" || uri.SchemeName() == L"https")) return value;
        }
        catch (...) {}
        return std::nullopt;
    }

    ::HaloDesktop::Api::Dto::MetaVideo ParseMetaVideo(
        winrt::Windows::Data::Json::JsonObject const& video)
    {
        auto const title = OptionalDisplayString(video, L"title", 512)
            .value_or(OptionalDisplayString(video, L"name", 512).value_or(L"Episode"));
        std::optional<std::int32_t> season;
        std::optional<std::int32_t> episode;
        if (video.HasKey(L"season"))
        {
            auto const value = video.GetNamedNumber(L"season");
            if (std::isfinite(value) && value >= 0 && std::floor(value) == value)
            {
                season = static_cast<std::int32_t>(value);
            }
        }
        if (video.HasKey(L"episode"))
        {
            auto const value = video.GetNamedNumber(L"episode");
            if (std::isfinite(value) && value >= 0 && std::floor(value) == value)
            {
                episode = static_cast<std::int32_t>(value);
            }
        }
        return {
            DisplayString(video, L"id", 2048),
            title,
            OptionalDisplayString(video, L"released", 128),
            OptionalHttpUrl(video, L"thumbnail"),
            OptionalDisplayString(video, L"overview", 4096),
            season,
            episode };
    }

    std::optional<::HaloDesktop::Api::Dto::StreamRecord> ParseStreamRecord(
        winrt::Windows::Data::Json::JsonObject const& stream)
    {
        auto const url = OptionalHttpUrl(stream, L"url");
        if (!url)
        {
            return std::nullopt;
        }

        ::HaloDesktop::Api::Dto::StreamRecord record;
        record.Url = *url;
        record.Name = OptionalDisplayString(stream, L"name", 2048);
        record.Title = OptionalDisplayString(stream, L"title", 4096);
        record.Description = OptionalDisplayString(stream, L"description", 4096);
        for (auto const& item : stream.GetNamedArray(L"subtitles", winrt::Windows::Data::Json::JsonArray{}))
        {
            if (item.ValueType() != winrt::Windows::Data::Json::JsonValueType::Object
                || record.Subtitles.size() == 128)
            {
                continue;
            }
            auto const subtitle = item.GetObject();
            auto const urlValue = OptionalHttpUrl(subtitle, L"url");
            auto const id = OptionalDisplayString(subtitle, L"id", 512);
            auto const lang = OptionalDisplayString(subtitle, L"lang", 32);
            if (urlValue && id && lang)
            {
                record.Subtitles.push_back({ *id, *urlValue, *lang });
            }
        }
        if (!stream.HasKey(L"behaviorHints"))
        {
            return record;
        }

        auto const hints = stream.GetNamedObject(L"behaviorHints");
        record.Filename = OptionalDisplayString(hints, L"filename", 1024);
        record.BingeGroup = OptionalDisplayString(hints, L"bingeGroup", 512);
        record.VideoHash = OptionalDisplayString(hints, L"videoHash", 64);
        if (hints.HasKey(L"videoSize"))
        {
            auto const value = hints.GetNamedNumber(L"videoSize");
            if (std::isfinite(value) && value > 0)
            {
                record.VideoSize = static_cast<std::uint64_t>(value);
            }
        }
        if (hints.HasKey(L"proxyHeaders"))
        {
            auto const proxyHeaders = hints.GetNamedObject(L"proxyHeaders");
            if (proxyHeaders.HasKey(L"request"))
            {
                for (auto const& pair : proxyHeaders.GetNamedObject(L"request"))
                {
                    if (pair.Value().ValueType() == winrt::Windows::Data::Json::JsonValueType::String
                        && record.RequestHeaders.size() < 64)
                    {
                        record.RequestHeaders.emplace_back(pair.Key(), pair.Value().GetString());
                    }
                }
            }
        }
        return record;
    }

    std::vector<::HaloDesktop::Api::Dto::AddonRecord::Catalog> Catalogs(
        winrt::Windows::Data::Json::JsonObject const& manifest)
    {
        std::vector<::HaloDesktop::Api::Dto::AddonRecord::Catalog> result;
        auto const catalogs = manifest.GetNamedArray(L"catalogs", winrt::Windows::Data::Json::JsonArray{});
        result.reserve((std::min)(catalogs.Size(), 128u));
        for (auto const& item : catalogs)
        {
            if (result.size() == 128) break;
            if (item.ValueType() != winrt::Windows::Data::Json::JsonValueType::Object) continue;
            auto const object = item.GetObject();
            auto const type = OptionalDisplayString(object, L"type", 128);
            auto const id = OptionalDisplayString(object, L"id", 512);
            if (!type || !id) continue;
            auto supportsSearch = false;
            auto hasRequired = false;
            for (auto const& extra : object.GetNamedArray(L"extra", winrt::Windows::Data::Json::JsonArray{}))
            {
                if (extra.ValueType() != winrt::Windows::Data::Json::JsonValueType::Object) continue;
                auto const extraObject = extra.GetObject();
                supportsSearch = supportsSearch || extraObject.GetNamedString(L"name", L"") == L"search";
                hasRequired = hasRequired || extraObject.GetNamedBoolean(L"isRequired", false);
            }
            for (auto const& extra : object.GetNamedArray(L"extraSupported", winrt::Windows::Data::Json::JsonArray{}))
            {
                supportsSearch = supportsSearch || (extra.ValueType() == winrt::Windows::Data::Json::JsonValueType::String && extra.GetString() == L"search");
            }
            hasRequired = hasRequired || object.GetNamedArray(L"extraRequired", winrt::Windows::Data::Json::JsonArray{}).Size() > 0;
            result.push_back({ *type, *id, OptionalDisplayString(object, L"name", 512), supportsSearch, hasRequired });
        }
        return result;
    }

    ::HaloDesktop::Api::Dto::AddonRecord ParseAddon(
        winrt::Windows::Data::Json::IJsonValue const& value,
        bool isGlobal)
    {
        auto const object = RequireObject(value, L"An addon entry must be an object.");
        auto const manifest = object.GetNamedObject(L"manifest");
        std::optional<winrt::hstring> transportUrl;
        if (object.HasKey(L"transportUrl"))
        {
            auto const url = object.GetNamedString(L"transportUrl");
            if (!url.empty())
            {
                transportUrl = url;
            }
        }
        auto const position = object.GetNamedNumber(L"position");
        if (!std::isfinite(position) || position < 0 || std::floor(position) != position
            || position > static_cast<double>((std::numeric_limits<std::int32_t>::max)()))
        {
            throw std::invalid_argument{ "The addon position is invalid." };
        }
        return ::HaloDesktop::Api::Dto::AddonRecord{
            .Id = RequireString(object, L"id"),
            .TransportUrl = transportUrl,
            .Name = DisplayString(manifest, L"name", 512),
            .Version = DisplayString(manifest, L"version", 64),
            .Resources = StringArray(manifest, L"resources"),
            .Types = StringArray(manifest, L"types"),
            .Catalogs = Catalogs(manifest),
            .Position = static_cast<std::int32_t>(position),
            .HideCatalogs = object.GetNamedBoolean(L"hideCatalogs", false),
            .IsGlobal = isGlobal,
        };
    }
}

namespace HaloDesktop::Api::Mappers
{
    bool ParseHealth(winrt::Windows::Data::Json::IJsonValue const& value)
    {
        auto const object = RequireObject(value, L"The health response must be a JSON object.");
        if (!object.GetNamedBoolean(L"ok"))
        {
            throw std::invalid_argument{ "The server reported an unhealthy response." };
        }
        return true;
    }

    Dto::AuthConfig ParseAuthConfig(winrt::Windows::Data::Json::IJsonValue const& value)
    {
        auto const object = RequireObject(value, L"The authentication response must be a JSON object.");
        auto const mode = RequireString(object, L"mode");

        if (mode == L"local")
        {
            return Dto::AuthConfig{ .Mode = Dto::AuthMode::Local };
        }
        if (mode != L"oidc")
        {
            throw std::invalid_argument{ "The server returned an unsupported authentication mode." };
        }

        Dto::AuthConfig result{
            .Mode = Dto::AuthMode::Oidc,
            .Issuer = RequireString(object, L"issuer"),
            .ClientId = RequireString(object, L"clientId"),
        };

        auto const scopes = object.GetNamedArray(L"scopes");
        result.Scopes.reserve(scopes.Size());
        for (auto const& scopeValue : scopes)
        {
            if (scopeValue.ValueType() != winrt::Windows::Data::Json::JsonValueType::String)
            {
                throw std::invalid_argument{ "The server returned an invalid authentication scope." };
            }
            auto const scope = Trimmed(scopeValue.GetString());
            if (scope.empty())
            {
                throw std::invalid_argument{ "The server returned an empty authentication scope." };
            }
            result.Scopes.push_back(scope);
        }
        return result;
    }

    Dto::IssuedToken ParseIssuedToken(winrt::Windows::Data::Json::IJsonValue const& value)
    {
        auto const object = RequireObject(value, L"The token response must be a JSON object.");
        return Dto::IssuedToken{
            .Token = RequireString(object, L"token"),
            .ExpiresAt = RequirePositiveInteger(object, L"expiresAt"),
        };
    }

    Dto::Me ParseMe(winrt::Windows::Data::Json::IJsonValue const& value)
    {
        auto const object = RequireObject(value, L"The account response must be a JSON object.");
        return Dto::Me{
            .Id = RequireString(object, L"id"),
            .Username = RequireString(object, L"username"),
            .IsAdmin = object.GetNamedBoolean(L"isAdmin"),
            .CreatedAt = RequirePositiveInteger(object, L"createdAt"),
        };
    }

    Dto::AddonsPayload ParseAddons(winrt::Windows::Data::Json::IJsonValue const& value)
    {
        auto const object = RequireObject(value, L"The addons response must be a JSON object.");
        Dto::AddonsPayload result;
        for (auto const& item : object.GetNamedArray(L"global"))
        {
            result.Global.push_back(ParseAddon(item, true));
        }
        for (auto const& item : object.GetNamedArray(L"user"))
        {
            result.User.push_back(ParseAddon(item, false));
        }
        return result;
    }

    Dto::SettingsPayload ParseSettings(winrt::Windows::Data::Json::IJsonValue const& value)
    {
        auto const object = RequireObject(value, L"The settings response must be a JSON object.");
        auto const updatedAt = object.GetNamedNumber(L"updatedAt");
        if (!std::isfinite(updatedAt) || updatedAt < 0 || std::floor(updatedAt) != updatedAt
            || updatedAt > static_cast<double>((std::numeric_limits<std::int64_t>::max)()))
        {
            throw std::invalid_argument{ "The settings response has an invalid timestamp." };
        }
        return Dto::SettingsPayload{
            .Value = object.GetNamedObject(L"value"),
            .UpdatedAt = static_cast<std::int64_t>(updatedAt),
        };
    }

    std::vector<Dto::MetaPreview> ParseCatalog(winrt::Windows::Data::Json::IJsonValue const& value)
    {
        auto const root = RequireObject(value, L"The catalog response must be an object.");
        auto const metas = root.GetNamedArray(L"metas");
        std::vector<Dto::MetaPreview> result;
        result.reserve((std::min)(metas.Size(), 30u));
        for (auto const& item : metas)
        {
            if (result.size() == 30) break;
            auto const object = RequireObject(item, L"A catalog item must be an object.");
            result.push_back(Dto::MetaPreview{
                .Id = DisplayString(object, L"id", 1024),
                .Type = DisplayString(object, L"type", 128),
                .Name = DisplayString(object, L"name", 512),
                .Poster = OptionalHttpUrl(object, L"poster"),
                .Background = OptionalHttpUrl(object, L"background"),
                .Description = OptionalDisplayString(object, L"description", 4096),
                .ReleaseInfo = OptionalDisplayString(object, L"releaseInfo", 128),
                .Rating = OptionalDisplayString(object, L"imdbRating", 32),
            });
        }
        return result;
    }

    std::vector<Dto::LibraryRow> ParseLibrary(winrt::Windows::Data::Json::IJsonValue const& value)
    {
        auto const array = value.GetArray();
        std::vector<Dto::LibraryRow> result;
        result.reserve(array.Size());
        for (auto const& item : array)
        {
            auto const object = RequireObject(item, L"A library row must be an object.");
            std::optional<std::int64_t> removedAt;
            if (object.HasKey(L"removedAt") && object.GetNamedValue(L"removedAt").ValueType() == winrt::Windows::Data::Json::JsonValueType::Number)
                removedAt = static_cast<std::int64_t>(object.GetNamedNumber(L"removedAt"));
            result.push_back({
                DisplayString(object, L"id", 2048), DisplayString(object, L"type", 128), DisplayString(object, L"name", 512),
                OptionalHttpUrl(object, L"poster"), RequirePositiveInteger(object, L"addedAt"), removedAt, RequirePositiveInteger(object, L"updatedAt") });
        }
        return result;
    }

    std::vector<Dto::WatchEntry> ParseWatchState(winrt::Windows::Data::Json::IJsonValue const& value)
    {
        auto const array = value.GetArray();
        std::vector<Dto::WatchEntry> result;
        result.reserve(array.Size());
        for (auto const& item : array)
        {
            auto const object = RequireObject(item, L"A watch-state row must be an object.");
            auto const position = object.GetNamedNumber(L"positionSec");
            auto const duration = object.GetNamedNumber(L"durationSec");
            if (!std::isfinite(position) || !std::isfinite(duration) || position < 0 || duration < 0) continue;
            result.push_back({
                DisplayString(object, L"videoId", 2048), DisplayString(object, L"itemId", 2048), position, duration,
                object.GetNamedBoolean(L"watched"), OptionalDisplayString(object, L"name", 512), OptionalHttpUrl(object, L"poster"),
                RequirePositiveInteger(object, L"updatedAt") });
        }
        return result;
    }

    Dto::MetaDetail ParseMeta(winrt::Windows::Data::Json::IJsonValue const& value)
    {
        auto const root=RequireObject(value,L"The metadata response must be an object.");auto const object=root.GetNamedObject(L"meta");
        Dto::MetaDetail result;
        result.Preview={DisplayString(object,L"id",1024),DisplayString(object,L"type",128),DisplayString(object,L"name",512),OptionalHttpUrl(object,L"poster"),OptionalHttpUrl(object,L"background"),OptionalDisplayString(object,L"description",4096),OptionalDisplayString(object,L"releaseInfo",128),OptionalDisplayString(object,L"imdbRating",32)};
        result.Runtime=OptionalDisplayString(object,L"runtime",128);result.Genres=StringArray(object,L"genres");result.Cast=StringArray(object,L"cast");result.Director=StringArray(object,L"director");result.Writer=StringArray(object,L"writer");
        for(auto const&item:object.GetNamedArray(L"videos",winrt::Windows::Data::Json::JsonArray{}))
        {
            if(item.ValueType()!=winrt::Windows::Data::Json::JsonValueType::Object)continue;
            result.Videos.push_back(ParseMetaVideo(item.GetObject()));
        }
        return result;
    }

    Dto::StreamsPayload ParseStreams(winrt::Windows::Data::Json::IJsonValue const& value)
    {
        auto const root=RequireObject(value,L"The streams response must be an object.");Dto::StreamsPayload result;
        for(auto const&entry:root.GetNamedArray(L"results")){auto const object=RequireObject(entry,L"A stream group must be an object.");auto const addon=object.GetNamedObject(L"addon");Dto::StreamGroup group{DisplayString(addon,L"id",1024),DisplayString(addon,L"name",80),{}};for(auto const&item:object.GetNamedArray(L"streams")){auto record=ParseStreamRecord(RequireObject(item,L"A stream must be an object."));if(record)group.Streams.push_back(std::move(*record));}if(!group.Streams.empty())result.Results.push_back(std::move(group));}
        for(auto const&entry:root.GetNamedArray(L"errors")){auto const object=RequireObject(entry,L"An addon error must be an object.");result.Errors.push_back({OptionalDisplayString(object,L"name",80),OptionalDisplayString(object,L"code",32)});}
        return result;
    }

    Dto::NextEpisodePayload ParseNextEpisode(winrt::Windows::Data::Json::IJsonValue const& value)
    {
        auto const root = RequireObject(value, L"The next-episode response must be an object.");
        Dto::NextEpisodePayload result;
        if (root.HasKey(L"video")
            && root.GetNamedValue(L"video").ValueType() == winrt::Windows::Data::Json::JsonValueType::Object)
        {
            result.Video = ParseMetaVideo(root.GetNamedObject(L"video"));
        }
        if (root.HasKey(L"stream")
            && root.GetNamedValue(L"stream").ValueType() == winrt::Windows::Data::Json::JsonValueType::Object)
        {
            result.Stream = ParseStreamRecord(root.GetNamedObject(L"stream"));
        }
        return result;
    }

    Dto::SubtitlesPayload ParseSubtitles(winrt::Windows::Data::Json::IJsonValue const&value)
    {
        auto const root=RequireObject(value,L"The subtitles response must be an object.");Dto::SubtitlesPayload result;result.HashMatched=root.GetNamedBoolean(L"hashMatched",false);
        for(auto const&entry:root.GetNamedArray(L"results")){auto const object=RequireObject(entry,L"A subtitle group must be an object.");auto const addon=object.GetNamedObject(L"addon");Dto::SubtitleGroup group{DisplayString(addon,L"id",1024),DisplayString(addon,L"name",80),{}};for(auto const&item:object.GetNamedArray(L"subtitles")){auto const subtitle=RequireObject(item,L"A subtitle must be an object.");auto const url=OptionalHttpUrl(subtitle,L"url");auto id=OptionalDisplayString(subtitle,L"id",512);auto lang=OptionalDisplayString(subtitle,L"lang",32);if(url&&id&&lang)group.Subtitles.push_back({*id,*url,*lang});}result.Results.push_back(std::move(group));}
        for(auto const&entry:root.GetNamedArray(L"errors")){auto const object=RequireObject(entry,L"An addon error must be an object.");result.Errors.push_back({OptionalDisplayString(object,L"name",80),OptionalDisplayString(object,L"code",32)});}return result;
    }
}
