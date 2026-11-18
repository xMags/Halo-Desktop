#include "pch.h"
#include "Api/Addons/AddonNormalize.h"

#include "Api/ActionEntryPolicy.h"

#include <algorithm>
#include <cmath>

namespace
{
    namespace Json = winrt::Windows::Data::Json;
    namespace Addons = HaloDesktop::Api::Addons;

    constexpr std::size_t MaximumResults = 500;
    constexpr std::size_t MaximumUrlLength = 16384;
    constexpr std::size_t MaximumLabelLength = 4096;
    constexpr std::size_t MaximumFilenameLength = 1024;
    constexpr std::size_t MaximumBingeGroupLength = 512;
    constexpr std::size_t MaximumHashLength = 128;
    constexpr std::size_t MaximumSubtitleIdLength = 512;
    constexpr std::size_t MaximumLanguageLength = 64;
    constexpr std::size_t MaximumHeaders = 64;
    constexpr std::size_t MaximumHeaderNameLength = 128;
    constexpr std::size_t MaximumHeaderValueLength = 4096;
    constexpr std::size_t MaximumAddonNameLength = 80;
    constexpr std::size_t MaximumCast = 40;
    constexpr std::size_t MaximumCastText = 200;
    constexpr std::size_t MaximumPhotoUrl = 1000;
    // Number.MAX_SAFE_INTEGER.
    constexpr double MaximumSafeInteger = 9007199254740991.0;

    [[noreturn]] void ThrowInvalidResponse()
    {
        throw Addons::AddonRequestFailure{ { Addons::AddonFailureKind::InvalidResponse, std::nullopt } };
    }

    bool IsObject(Json::IJsonValue const& value)
    {
        return value && value.ValueType() == Json::JsonValueType::Object;
    }

    bool IsArray(Json::IJsonValue const& value)
    {
        return value && value.ValueType() == Json::JsonValueType::Array;
    }

    Json::IJsonValue Field(Json::JsonObject const& object, wchar_t const* key)
    {
        return object.HasKey(key) ? object.GetNamedValue(key) : Json::IJsonValue{ nullptr };
    }

    std::optional<std::wstring> StringField(Json::JsonObject const& object, wchar_t const* key)
    {
        auto const value = Field(object, key);
        if (!value || value.ValueType() != Json::JsonValueType::String) return std::nullopt;
        return std::wstring{ value.GetString() };
    }

    // optionalString: kept only when it is a string no longer than the bound.
    void CopyBoundedString(
        Json::JsonObject const& source,
        Json::JsonObject const& target,
        wchar_t const* key,
        std::size_t maximumLength)
    {
        auto const text = StringField(source, key);
        if (text && text->size() <= maximumLength)
        {
            target.Insert(key, Json::JsonValue::CreateStringValue(*text));
        }
    }

    // requiredString: a non-empty string no longer than the bound.
    std::optional<std::wstring> RequiredString(Json::JsonObject const& object, wchar_t const* key, std::size_t maximumLength)
    {
        auto text = StringField(object, key);
        if (!text || text->empty() || text->size() > maximumLength) return std::nullopt;
        return text;
    }

    std::optional<std::wstring> ValidHttpUrl(Json::JsonObject const& object, wchar_t const* key)
    {
        auto text = StringField(object, key);
        if (!text || text->empty() || text->size() > MaximumUrlLength || !Addons::IsHttpUrl(*text)) return std::nullopt;
        return text;
    }

    bool HasForbiddenHeaderCharacter(std::wstring_view text)
    {
        return text.find_first_of(std::wstring_view{ L"\r\n\0", 3 }) != std::wstring_view::npos;
    }

    std::optional<Json::JsonObject> NormalizeHeaderMap(Json::IJsonValue const& value)
    {
        if (!IsObject(value)) return std::nullopt;
        Json::JsonObject result;
        std::size_t considered{};
        for (auto const& entry : value.GetObject())
        {
            if (considered++ >= MaximumHeaders) break;
            std::wstring_view const name{ entry.Key() };
            if (name.empty() || name.size() > MaximumHeaderNameLength || HasForbiddenHeaderCharacter(name)) continue;
            auto const headerValue = entry.Value();
            if (!headerValue || headerValue.ValueType() != Json::JsonValueType::String) continue;
            auto const text = headerValue.GetString();
            if (text.size() > MaximumHeaderValueLength || HasForbiddenHeaderCharacter(std::wstring_view{ text })) continue;
            result.Insert(entry.Key(), Json::JsonValue::CreateStringValue(text));
        }
        if (result.Size() == 0) return std::nullopt;
        return result;
    }

    std::optional<Json::JsonObject> NormalizeProxyHeaders(Json::IJsonValue const& value)
    {
        if (!IsObject(value)) return std::nullopt;
        auto const object = value.GetObject();
        auto const request = NormalizeHeaderMap(Field(object, L"request"));
        auto const response = NormalizeHeaderMap(Field(object, L"response"));
        if (!request && !response) return std::nullopt;
        Json::JsonObject result;
        if (request) result.Insert(L"request", *request);
        if (response) result.Insert(L"response", *response);
        return result;
    }

    std::optional<Json::JsonObject> NormalizeBehaviorHints(Json::IJsonValue const& value)
    {
        if (!IsObject(value)) return std::nullopt;
        auto const hints = value.GetObject();
        Json::JsonObject result;
        if (auto const notWebReady = Field(hints, L"notWebReady");
            notWebReady && notWebReady.ValueType() == Json::JsonValueType::Boolean)
        {
            result.Insert(L"notWebReady", Json::JsonValue::CreateBooleanValue(notWebReady.GetBoolean()));
        }
        CopyBoundedString(hints, result, L"bingeGroup", MaximumBingeGroupLength);
        if (auto const proxyHeaders = NormalizeProxyHeaders(Field(hints, L"proxyHeaders")))
        {
            result.Insert(L"proxyHeaders", *proxyHeaders);
        }
        CopyBoundedString(hints, result, L"filename", MaximumFilenameLength);
        if (auto const videoSize = Field(hints, L"videoSize");
            videoSize && videoSize.ValueType() == Json::JsonValueType::Number)
        {
            auto const size = videoSize.GetNumber();
            if (std::isfinite(size) && std::floor(size) == size && size > 0 && size <= MaximumSafeInteger)
            {
                result.Insert(L"videoSize", Json::JsonValue::CreateNumberValue(size));
            }
        }
        CopyBoundedString(hints, result, L"videoHash", MaximumHashLength);
        if (result.Size() == 0) return std::nullopt;
        return result;
    }

    Json::JsonArray NormalizeSubtitleList(Json::JsonArray const& values)
    {
        Json::JsonArray result;
        std::size_t considered{};
        for (auto const& value : values)
        {
            if (considered++ >= MaximumResults) break;
            if (!IsObject(value)) continue;
            auto const subtitle = value.GetObject();
            auto const id = RequiredString(subtitle, L"id", MaximumSubtitleIdLength);
            auto const url = ValidHttpUrl(subtitle, L"url");
            auto const lang = RequiredString(subtitle, L"lang", MaximumLanguageLength);
            if (!id || !url || !lang) continue;
            Json::JsonObject entry;
            entry.Insert(L"id", Json::JsonValue::CreateStringValue(*id));
            entry.Insert(L"url", Json::JsonValue::CreateStringValue(*url));
            entry.Insert(L"lang", Json::JsonValue::CreateStringValue(*lang));
            result.Append(entry);
        }
        return result;
    }

    std::optional<Json::JsonObject> NormalizeStream(Json::IJsonValue const& value)
    {
        if (!IsObject(value)) return std::nullopt;
        auto const stream = value.GetObject();
        auto const url = ValidHttpUrl(stream, L"url");
        if (!url) return std::nullopt;
        if (auto const infoHash = Field(stream, L"infoHash"); infoHash && infoHash.ValueType() == Json::JsonValueType::String)
        {
            return std::nullopt;
        }

        Json::JsonObject result;
        result.Insert(L"url", Json::JsonValue::CreateStringValue(*url));
        CopyBoundedString(stream, result, L"name", MaximumLabelLength);
        CopyBoundedString(stream, result, L"title", MaximumLabelLength);
        CopyBoundedString(stream, result, L"description", MaximumLabelLength);
        if (auto const subtitles = Field(stream, L"subtitles"); IsArray(subtitles))
        {
            result.Insert(L"subtitles", NormalizeSubtitleList(subtitles.GetArray()));
        }
        if (auto const hints = NormalizeBehaviorHints(Field(stream, L"behaviorHints")))
        {
            result.Insert(L"behaviorHints", *hints);
        }
        return result;
    }

    bool IsActionStreamObject(Json::JsonObject const& stream)
    {
        auto const url = StringField(stream, L"url").value_or(std::wstring{});
        auto const name = StringField(stream, L"name").value_or(std::wstring{});
        auto const title = StringField(stream, L"title").value_or(std::wstring{});
        auto const description = StringField(stream, L"description").value_or(std::wstring{});
        return ::HaloDesktop::Api::IsActionStream({
            .Url = url,
            .Name = name,
            .Title = title,
            .Description = description,
        });
    }

    // boundedText: trimmed, non-empty and no longer than the bound.
    std::optional<std::wstring> BoundedCastText(Json::JsonObject const& object, wchar_t const* key)
    {
        auto const text = StringField(object, key);
        if (!text) return std::nullopt;
        auto trimmed = Addons::JsTrim(*text);
        if (trimmed.empty() || trimmed.size() > MaximumCastText) return std::nullopt;
        return trimmed;
    }

    // JavaScript truthiness of a JSON value.
    bool IsTruthy(Json::IJsonValue const& value)
    {
        if (!value) return false;
        switch (value.ValueType())
        {
        case Json::JsonValueType::Null: return false;
        case Json::JsonValueType::Boolean: return value.GetBoolean();
        case Json::JsonValueType::Number: return value.GetNumber() != 0;
        case Json::JsonValueType::String: return !value.GetString().empty();
        default: return true;
        }
    }

    std::vector<std::wstring> StringList(Json::JsonArray const& values)
    {
        std::vector<std::wstring> result;
        for (auto const& value : values)
        {
            if (value && value.ValueType() == Json::JsonValueType::String) result.emplace_back(value.GetString());
        }
        return result;
    }

    // Absent or null: none (no constraint, or the manifest's own); a list:
    // its strings; anything else matches nothing.
    std::optional<std::vector<std::wstring>> PrefixList(Json::JsonObject const& object)
    {
        auto const value = Field(object, L"idPrefixes");
        if (!value || value.ValueType() == Json::JsonValueType::Null) return std::nullopt;
        if (!IsArray(value)) return std::vector<std::wstring>{};
        return StringList(value.GetArray());
    }
}

namespace HaloDesktop::Api::Addons
{
    std::optional<Json::JsonObject> ParseManifest(Json::IJsonValue const& value)
    {
        if (!IsObject(value)) return std::nullopt;
        auto const manifest = value.GetObject();
        auto const id = StringField(manifest, L"id");
        auto const name = StringField(manifest, L"name");
        if (!id || id->empty() || !StringField(manifest, L"version") || !name) return std::nullopt;
        // Stricter than the reference, which takes a name of nothing but
        // white space: an addon is installed only with a name it can be shown
        // and removed by.
        if (auto const trimmedName = JsTrim(*name); trimmedName.empty() || trimmedName.size() > MaximumLabelLength)
        {
            return std::nullopt;
        }

        auto const types = Field(manifest, L"types");
        if (!IsArray(types)) return std::nullopt;
        for (auto const& type : types.GetArray())
        {
            if (!type || type.ValueType() != Json::JsonValueType::String) return std::nullopt;
        }

        auto const resources = Field(manifest, L"resources");
        if (!IsArray(resources)) return std::nullopt;
        for (auto const& entry : resources.GetArray())
        {
            if (entry && entry.ValueType() == Json::JsonValueType::String) continue;
            if (!IsObject(entry) || !StringField(entry.GetObject(), L"name")) return std::nullopt;
        }

        auto const catalogs = Field(manifest, L"catalogs");
        if (!IsArray(catalogs)) return std::nullopt;
        for (auto const& entry : catalogs.GetArray())
        {
            if (!IsObject(entry)) return std::nullopt;
            auto const catalog = entry.GetObject();
            if (!StringField(catalog, L"type") || !StringField(catalog, L"id")) return std::nullopt;
        }
        return manifest;
    }

    ManifestFacts ReadManifestFacts(Json::JsonObject const& manifest)
    {
        ManifestFacts facts;
        facts.Id = StringField(manifest, L"id").value_or(std::wstring{});
        if (auto const types = Field(manifest, L"types"); IsArray(types)) facts.Types = StringList(types.GetArray());
        facts.IdPrefixes = PrefixList(manifest);
        if (auto const resources = Field(manifest, L"resources"); IsArray(resources))
        {
            for (auto const& entry : resources.GetArray())
            {
                if (entry && entry.ValueType() == Json::JsonValueType::String)
                {
                    facts.Resources.push_back({ std::wstring{ entry.GetString() }, true, {}, std::nullopt });
                    continue;
                }
                if (!IsObject(entry)) continue;
                auto const resource = entry.GetObject();
                auto const name = StringField(resource, L"name");
                if (!name) continue;
                ManifestResourceEntry parsed{ *name, false, {}, PrefixList(resource) };
                if (auto const types = Field(resource, L"types"); IsArray(types)) parsed.Types = StringList(types.GetArray());
                facts.Resources.push_back(std::move(parsed));
            }
        }
        return facts;
    }

    std::optional<std::wstring> ManifestName(Json::JsonObject const& manifest)
    {
        return StringField(manifest, L"name");
    }

    std::wstring SafeAddonName(std::optional<std::wstring_view> name)
    {
        constexpr wchar_t const* fallback = L"Addon";
        if (!name) return fallback;

        // Control characters to spaces, then every run of white space to one.
        std::wstring collapsed;
        collapsed.reserve(name->size());
        auto inSpace = false;
        for (auto character : *name)
        {
            if (character <= 0x1F || character == 0x7F) character = L' ';
            if (IsJsWhitespace(character))
            {
                if (!inSpace) collapsed.push_back(L' ');
                inSpace = true;
                continue;
            }
            inSpace = false;
            collapsed.push_back(character);
        }
        auto cleaned = JsTrim(collapsed);

        std::wstring lowered{ cleaned };
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](wchar_t character)
        {
            return character >= L'A' && character <= L'Z' ? static_cast<wchar_t>(character - L'A' + L'a') : character;
        });
        if (lowered.find(L"http://") != std::wstring::npos || lowered.find(L"https://") != std::wstring::npos
            || cleaned.find_first_of(L"/\\") != std::wstring::npos)
        {
            return fallback;
        }
        std::size_t run{};
        for (auto const character : cleaned)
        {
            auto const tokenCharacter = (character >= L'A' && character <= L'Z') || (character >= L'a' && character <= L'z')
                || (character >= L'0' && character <= L'9') || character == L'_' || character == L'-';
            run = tokenCharacter ? run + 1 : 0;
            if (run >= 32) return fallback;
        }
        if (cleaned.size() > MaximumAddonNameLength) cleaned.resize(MaximumAddonNameLength);
        return cleaned.empty() ? std::wstring{ fallback } : cleaned;
    }

    ClassifiedFailure ClassifyAddonFailure(AddonFailure const& failure)
    {
        switch (failure.Kind)
        {
        case AddonFailureKind::Timeout:
            return { L"timeout", L"Addon timed out.", std::nullopt };
        case AddonFailureKind::UpstreamHttp:
        {
            auto const status = failure.Status && *failure.Status >= 100 && *failure.Status <= 599
                ? failure.Status
                : std::nullopt;
            return {
                L"upstream_http",
                status ? L"Addon returned HTTP " + std::to_wstring(*status) + L"." : L"Addon returned an HTTP error.",
                status,
            };
        }
        case AddonFailureKind::BlockedTarget:
            return { L"blocked_target", L"Addon target was blocked for safety.", std::nullopt };
        case AddonFailureKind::InvalidResponse:
            return { L"invalid_response", L"Addon returned an invalid response.", std::nullopt };
        case AddonFailureKind::Unavailable:
            break;
        }
        return { L"unavailable", L"Addon is unavailable.", std::nullopt };
    }

    Json::JsonObject AddonErrorJson(
        std::wstring_view addonId,
        std::optional<std::wstring_view> addonName,
        AddonFailure const& failure)
    {
        auto const classified = ClassifyAddonFailure(failure);
        Json::JsonObject error;
        error.Insert(L"id", Json::JsonValue::CreateStringValue(addonId));
        error.Insert(L"name", Json::JsonValue::CreateStringValue(SafeAddonName(addonName)));
        error.Insert(L"code", Json::JsonValue::CreateStringValue(classified.Code));
        if (classified.Status)
        {
            error.Insert(L"status", Json::JsonValue::CreateNumberValue(static_cast<double>(*classified.Status)));
        }
        error.Insert(L"message", Json::JsonValue::CreateStringValue(classified.Message));
        return error;
    }

    Json::JsonArray NormalizeStreamsResponse(Json::IJsonValue const& value)
    {
        if (!IsObject(value)) ThrowInvalidResponse();
        auto const streams = Field(value.GetObject(), L"streams");
        if (!IsArray(streams)) ThrowInvalidResponse();
        Json::JsonArray result;
        std::size_t considered{};
        for (auto const& entry : streams.GetArray())
        {
            if (considered++ >= MaximumResults) break;
            auto const stream = NormalizeStream(entry);
            // A stream that would act (delete files, clear a watchlist) is no video.
            if (stream && !IsActionStreamObject(*stream)) result.Append(*stream);
        }
        return result;
    }

    Json::JsonArray NormalizeSubtitlesResponse(Json::IJsonValue const& value)
    {
        if (!IsObject(value)) ThrowInvalidResponse();
        auto const subtitles = Field(value.GetObject(), L"subtitles");
        if (!IsArray(subtitles)) ThrowInvalidResponse();
        return NormalizeSubtitleList(subtitles.GetArray());
    }

    std::optional<Json::JsonArray> SanitizeCast(Json::IJsonValue const& value)
    {
        if (!IsArray(value)) return std::nullopt;
        Json::JsonArray cast;
        auto pictured = false;
        for (auto const& entry : value.GetArray())
        {
            if (cast.Size() >= MaximumCast) break;
            if (!IsObject(entry)) continue;
            auto const person = entry.GetObject();
            auto const name = BoundedCastText(person, L"name");
            if (!name) continue;
            Json::JsonObject clean;
            clean.Insert(L"name", Json::JsonValue::CreateStringValue(*name));
            if (auto const part = BoundedCastText(person, L"character"))
            {
                clean.Insert(L"character", Json::JsonValue::CreateStringValue(*part));
            }
            if (auto const photo = StringField(person, L"photo");
                photo && photo->size() <= MaximumPhotoUrl && IsHttpsUrl(*photo))
            {
                clean.Insert(L"photo", Json::JsonValue::CreateStringValue(*photo));
                pictured = true;
            }
            cast.Append(clean);
        }
        if (!pictured) return std::nullopt;
        return cast;
    }

    bool DescribesTitle(Json::IJsonValue const& value)
    {
        if (!IsObject(value)) return false;
        auto const meta = Field(value.GetObject(), L"meta");
        if (!IsObject(meta)) return false;
        auto const id = StringField(meta.GetObject(), L"id");
        return id && !id->empty();
    }

    bool HasPicturedCast(Json::JsonObject const& meta)
    {
        auto const extras = Field(meta, L"app_extras");
        if (!IsObject(extras)) return false;
        auto const cast = Field(extras.GetObject(), L"cast");
        if (!IsArray(cast)) return false;
        for (auto const& person : cast.GetArray())
        {
            if (IsObject(person) && IsTruthy(Field(person.GetObject(), L"photo"))) return true;
        }
        return false;
    }

    std::vector<VideoOrderKey> VideoOrderKeys(Json::JsonArray const& videos)
    {
        std::vector<VideoOrderKey> keys;
        keys.reserve(videos.Size());
        for (auto const& entry : videos)
        {
            VideoOrderKey key;
            if (IsObject(entry))
            {
                auto const video = entry.GetObject();
                key.Id = StringField(video, L"id");
                auto const number = [&video](wchar_t const* name) -> std::optional<double>
                {
                    auto const field = Field(video, name);
                    if (!field || field.ValueType() != Json::JsonValueType::Number) return std::nullopt;
                    return field.GetNumber();
                };
                key.Season = number(L"season");
                key.Episode = number(L"episode");
                auto const released = Field(video, L"released");
                if (released && released.ValueType() == Json::JsonValueType::String)
                {
                    key.ReleasedAtMs = ParseIsoDateMs(std::wstring_view{ released.GetString() });
                }
                else if (released && released.ValueType() == Json::JsonValueType::Number
                    && std::isfinite(released.GetNumber()) && std::abs(released.GetNumber()) <= 8.64e15
                    && released.GetNumber() != 0)
                {
                    key.ReleasedAtMs = static_cast<std::int64_t>(released.GetNumber());
                }
            }
            keys.push_back(std::move(key));
        }
        return keys;
    }

    std::optional<Json::JsonArray> WithoutActionEntries(Json::IJsonValue const& metas)
    {
        if (!IsArray(metas)) return std::nullopt;
        Json::JsonArray result;
        for (auto const& entry : metas.GetArray())
        {
            if (!entry || entry.ValueType() == Json::JsonValueType::Null) return std::nullopt;
            if (IsObject(entry))
            {
                auto const meta = entry.GetObject();
                auto const id = StringField(meta, L"id").value_or(std::wstring{});
                auto const name = StringField(meta, L"name").value_or(std::wstring{});
                if (::HaloDesktop::Api::IsActionEntry(id, name)) continue;
            }
            result.Append(entry);
        }
        return result;
    }
}
