#include "pch.h"
#include "Api/ActionEntryPolicy.h"
#include "Api/Dto.h"
#include "Api/JsonNumberPolicy.h"
#include "Security/ProtectedHttpHeaders.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
    // JSON numbers are doubles. 2^64 is the first value outside uint64_t, so it
    // is an exclusive ceiling. Comparing against uint64_t::max converted to a
    // double is unsafe because that conversion rounds up to this same value.
    constexpr double Uint64ExclusiveUpperBound = 18446744073709551616.0;

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
        auto const value = ::HaloDesktop::Api::CheckedPositiveInt64(object.GetNamedNumber(key));
        if (!value)
        {
            throw std::invalid_argument{ "The server returned an invalid integer field." };
        }
        return *value;
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

    std::vector<winrt::hstring> OptionalStringArray(
        winrt::Windows::Data::Json::JsonObject const& object,
        wchar_t const* key)
    {
        if (!object.HasKey(key)
            || object.GetNamedValue(key).ValueType() != winrt::Windows::Data::Json::JsonValueType::Array)
        {
            return {};
        }
        return StringArray(object, key);
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

    std::optional<std::int32_t> OptionalNonNegativeInteger(
        winrt::Windows::Data::Json::JsonObject const& object,
        wchar_t const* key)
    {
        if (!object.HasKey(key))
        {
            return std::nullopt;
        }
        auto const field = object.GetNamedValue(key);
        if (field.ValueType() != winrt::Windows::Data::Json::JsonValueType::Number)
        {
            return std::nullopt;
        }
        auto const value = field.GetNumber();
        if (!std::isfinite(value) || value < 0 || std::floor(value) != value
            || value > static_cast<double>((std::numeric_limits<std::int32_t>::max)()))
        {
            return std::nullopt;
        }
        return static_cast<std::int32_t>(value);
    }

    // The helpers below serve fields added after the original wire contract.
    // Each one is fail-soft: a value of the wrong type, out of bounds or
    // malformed is dropped, so an addon's odd optional field can never fail a
    // payload that parsed before the field was read.

    std::optional<winrt::Windows::Data::Json::JsonObject> OptionalObject(
        winrt::Windows::Data::Json::JsonObject const& object,
        wchar_t const* key)
    {
        if (!object.HasKey(key)) return std::nullopt;
        auto const value = object.GetNamedValue(key);
        if (value.ValueType() != winrt::Windows::Data::Json::JsonValueType::Object) return std::nullopt;
        return value.GetObject();
    }

    std::optional<winrt::Windows::Data::Json::JsonArray> OptionalArray(
        winrt::Windows::Data::Json::JsonObject const& object,
        wchar_t const* key)
    {
        if (!object.HasKey(key)) return std::nullopt;
        auto const value = object.GetNamedValue(key);
        if (value.ValueType() != winrt::Windows::Data::Json::JsonValueType::Array) return std::nullopt;
        return value.GetArray();
    }

    bool OptionalBoolean(
        winrt::Windows::Data::Json::JsonObject const& object,
        wchar_t const* key,
        bool fallback)
    {
        if (!object.HasKey(key)) return fallback;
        auto const value = object.GetNamedValue(key);
        if (value.ValueType() != winrt::Windows::Data::Json::JsonValueType::Boolean) return fallback;
        return value.GetBoolean();
    }

    // The string exactly as sent, for the action-entry patterns, which judge
    // what the addon wrote rather than its display form. Empty when absent.
    winrt::hstring RawString(
        winrt::Windows::Data::Json::JsonObject const& object,
        wchar_t const* key)
    {
        if (!object.HasKey(key)) return {};
        auto const value = object.GetNamedValue(key);
        if (value.ValueType() != winrt::Windows::Data::Json::JsonValueType::String) return {};
        return value.GetString();
    }

    // Trimmed, control characters replaced, and rejected rather than cut when
    // longer than the bound: a clipped id or name would name something else.
    std::optional<winrt::hstring> BoundedText(
        winrt::Windows::Data::Json::IJsonValue const& value,
        std::size_t maximumLength)
    {
        if (!value || value.ValueType() != winrt::Windows::Data::Json::JsonValueType::String) return std::nullopt;
        std::wstring text{ Trimmed(value.GetString()) };
        if (text.empty() || text.size() > maximumLength) return std::nullopt;
        std::replace_if(text.begin(), text.end(), [](wchar_t character) { return std::iswcntrl(character) != 0; }, L' ');
        return winrt::hstring{ text };
    }

    std::optional<winrt::hstring> OptionalBoundedText(
        winrt::Windows::Data::Json::JsonObject const& object,
        wchar_t const* key,
        std::size_t maximumLength)
    {
        if (!object.HasKey(key)) return std::nullopt;
        return BoundedText(object.GetNamedValue(key), maximumLength);
    }

    // Strings, or objects naming one (`{ "name": ... }`), the way addons list
    // genres and options; anything else in the array is skipped.
    std::vector<winrt::hstring> TolerantStringArray(
        winrt::Windows::Data::Json::JsonObject const& object,
        wchar_t const* key,
        std::size_t maximumItems,
        std::size_t maximumLength)
    {
        std::vector<winrt::hstring> result;
        auto const array = OptionalArray(object, key);
        if (!array) return result;
        for (auto const& item : *array)
        {
            if (result.size() == maximumItems) break;
            auto const text = item.ValueType() == winrt::Windows::Data::Json::JsonValueType::Object
                ? OptionalBoundedText(item.GetObject(), L"name", maximumLength)
                : BoundedText(item, maximumLength);
            if (text) result.push_back(*text);
        }
        return result;
    }

    std::optional<winrt::hstring> OptionalHttpsUrl(
        winrt::Windows::Data::Json::JsonObject const& object,
        wchar_t const* key,
        std::size_t maximumLength)
    {
        if (!object.HasKey(key)) return std::nullopt;
        auto const value = object.GetNamedValue(key);
        if (value.ValueType() != winrt::Windows::Data::Json::JsonValueType::String) return std::nullopt;
        auto const text = value.GetString();
        if (text.empty() || text.size() > maximumLength) return std::nullopt;
        try
        {
            winrt::Windows::Foundation::Uri const uri{ text };
            if (!uri.Host().empty() && uri.SchemeName() == L"https") return text;
        }
        catch (...) {}
        return std::nullopt;
    }

    // `tt` and digits: the bare IMDb id shape.
    bool IsImdbId(winrt::hstring const& value)
    {
        std::wstring_view const text{ value };
        if (text.size() < 3 || !text.starts_with(L"tt")) return false;
        return std::all_of(text.begin() + 2, text.end(), [](wchar_t character)
        {
            return character >= L'0' && character <= L'9';
        });
    }

    ::HaloDesktop::Api::Dto::MetaVideo ParseMetaVideo(
        winrt::Windows::Data::Json::JsonObject const& video)
    {
        // Trimmed, so a title of nothing but spaces counts as none and the
        // fallbacks below apply.
        auto const ownText = [&video](wchar_t const* key) -> std::optional<winrt::hstring>
        {
            auto const text = OptionalDisplayString(video, key, 512);
            if (!text) return std::nullopt;
            auto trimmed = Trimmed(*text);
            return trimmed.empty() ? std::nullopt : std::optional<winrt::hstring>{ std::move(trimmed) };
        };
        auto const ownTitle = ownText(L"title");
        auto const ownName = ownText(L"name");
        return {
            DisplayString(video, L"id", 2048),
            // Empty for an untitled episode: each reader picks its own fallback,
            // as the reference does (the id in episode rows, the show's name in
            // the sources sheet and the player, nothing on a continue card).
            ownTitle.value_or(ownName.value_or(winrt::hstring{})),
            OptionalDisplayString(video, L"released", 128),
            OptionalHttpUrl(video, L"thumbnail"),
            OptionalDisplayString(video, L"overview", 4096),
            OptionalNonNegativeInteger(video, L"season"),
            OptionalNonNegativeInteger(video, L"episode"),
            ownTitle.has_value() || ownName.has_value() };
    }

    // The fields a catalog preview and a title's details share. Genres are
    // read by each caller, since the details always parsed them more strictly.
    ::HaloDesktop::Api::Dto::MetaPreview ParsePreview(
        winrt::Windows::Data::Json::JsonObject const& object)
    {
        ::HaloDesktop::Api::Dto::MetaPreview preview{
            .Id = DisplayString(object, L"id", 1024),
            .Type = DisplayString(object, L"type", 128),
            .Name = DisplayString(object, L"name", 512),
            .Poster = OptionalHttpUrl(object, L"poster"),
            .Background = OptionalHttpUrl(object, L"background"),
            .Description = OptionalDisplayString(object, L"description", 4096),
            .ReleaseInfo = OptionalDisplayString(object, L"releaseInfo", 128),
            .Rating = OptionalDisplayString(object, L"imdbRating", 32),
            .Logo = OptionalHttpUrl(object, L"logo"),
        };
        if (auto const hints = OptionalObject(object, L"behaviorHints"))
        {
            preview.DefaultVideoId = OptionalBoundedText(*hints, L"defaultVideoId", 2048);
        }
        if (auto const imdbId = OptionalBoundedText(object, L"imdb_id", 32); imdbId && IsImdbId(*imdbId))
        {
            preview.ImdbId = imdbId;
        }
        return preview;
    }

    // A stream that acts instead of playing, judged on the fields as sent.
    bool IsActionStreamObject(winrt::Windows::Data::Json::JsonObject const& stream)
    {
        auto const url = RawString(stream, L"url");
        auto const name = RawString(stream, L"name");
        auto const title = RawString(stream, L"title");
        auto const description = RawString(stream, L"description");
        return ::HaloDesktop::Api::IsActionStream({
            .Url = url,
            .Name = name,
            .Title = title,
            .Description = description,
        });
    }

    std::optional<::HaloDesktop::Api::Dto::StreamRecord> ParseStreamRecord(
        winrt::Windows::Data::Json::JsonObject const& stream)
    {
        auto const url = OptionalHttpUrl(stream, L"url");
        if (!url || IsActionStreamObject(stream))
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
            auto const field = hints.GetNamedValue(L"videoSize");
            if (field.ValueType() == winrt::Windows::Data::Json::JsonValueType::Number)
            {
                // Whole bytes, as the reference's wholeBytes: a fractional figure
                // is rounded rather than dropped, and zero means unknown.
                auto const value = field.GetNumber();
                auto const rounded = std::isfinite(value) ? std::round(value) : 0.0;
                if (rounded > 0 && rounded < Uint64ExclusiveUpperBound)
                {
                    record.VideoSize = static_cast<std::uint64_t>(rounded);
                }
            }
        }
        if (hints.HasKey(L"proxyHeaders"))
        {
            auto const proxyHeaders = hints.GetNamedObject(L"proxyHeaders");
            if (proxyHeaders.HasKey(L"request"))
            {
                // As the reference's `playbackHeaders`: the first 64 entries are
                // considered, and each unusable one is dropped on its own, so a bad
                // header costs only itself and the stream still plays.
                std::size_t considered{};
                for (auto const& pair : proxyHeaders.GetNamedObject(L"request"))
                {
                    if (considered++ >= 64)
                    {
                        break;
                    }
                    if (pair.Value().ValueType() != winrt::Windows::Data::Json::JsonValueType::String)
                    {
                        continue;
                    }
                    auto const value = pair.Value().GetString();
                    if (::HaloDesktop::Security::IsUsableProtectedHttpHeader(std::wstring{ pair.Key() }, std::wstring{ value }))
                    {
                        record.RequestHeaders.emplace_back(pair.Key(), value);
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
            // The reads that derive SupportsSearch and HasRequiredExtra keep their
            // original strictness; only the values added beside them are fail-soft.
            auto supportsSearch = false;
            auto hasRequired = false;
            std::vector<::HaloDesktop::Api::Dto::AddonRecord::CatalogExtra> extras;
            for (auto const& extra : object.GetNamedArray(L"extra", winrt::Windows::Data::Json::JsonArray{}))
            {
                if (extra.ValueType() != winrt::Windows::Data::Json::JsonValueType::Object) continue;
                auto const extraObject = extra.GetObject();
                auto const name = extraObject.GetNamedString(L"name", L"");
                auto const isRequired = extraObject.GetNamedBoolean(L"isRequired", false);
                supportsSearch = supportsSearch || name == L"search";
                hasRequired = hasRequired || isRequired;
                if (name.empty() || name.size() > 128 || extras.size() == 32) continue;
                extras.push_back({ name, isRequired, TolerantStringArray(extraObject, L"options", 512, 128) });
            }
            std::vector<winrt::hstring> extraSupported;
            for (auto const& extra : object.GetNamedArray(L"extraSupported", winrt::Windows::Data::Json::JsonArray{}))
            {
                if (extra.ValueType() != winrt::Windows::Data::Json::JsonValueType::String) continue;
                auto const name = extra.GetString();
                supportsSearch = supportsSearch || name == L"search";
                if (!name.empty() && name.size() <= 128 && extraSupported.size() < 32) extraSupported.push_back(name);
            }
            auto const requiredArray = object.GetNamedArray(L"extraRequired", winrt::Windows::Data::Json::JsonArray{});
            hasRequired = hasRequired || requiredArray.Size() > 0;
            std::vector<winrt::hstring> extraRequired;
            for (auto const& extra : requiredArray)
            {
                if (extra.ValueType() != winrt::Windows::Data::Json::JsonValueType::String) continue;
                auto const name = extra.GetString();
                if (!name.empty() && name.size() <= 128 && extraRequired.size() < 32) extraRequired.push_back(name);
            }
            result.push_back({
                *type,
                *id,
                OptionalDisplayString(object, L"name", 512),
                supportsSearch,
                hasRequired,
                std::move(extras),
                std::move(extraSupported),
                std::move(extraRequired),
            });
        }
        return result;
    }

    ::HaloDesktop::Api::Dto::AddonRecord ParseAddon(
        winrt::Windows::Data::Json::IJsonValue const& value,
        bool isGlobal)
    {
        auto const object = RequireObject(value, L"An addon entry must be an object.");
        auto const manifest = object.GetNamedObject(L"manifest");
        // A null or non-string URL is a row without one, as a missing key is:
        // the server hides it, and that must not fail the whole list.
        std::optional<winrt::hstring> transportUrl;
        if (auto const url = RawString(object, L"transportUrl"); !url.empty())
        {
            transportUrl = url;
        }
        auto const position = object.GetNamedNumber(L"position");
        if (!std::isfinite(position) || position < 0 || std::floor(position) != position
            || position >= static_cast<double>(1ULL << 31))
        {
            throw std::invalid_argument{ "The addon position is invalid." };
        }
        return ::HaloDesktop::Api::Dto::AddonRecord{
            .Id = RequireString(object, L"id"),
            // Fail-soft: an addon that omits its manifest id stays usable and is
            // simply never merged with another row.
            .ManifestId = OptionalDisplayString(manifest, L"id", 512).value_or(L""),
            .TransportUrl = transportUrl,
            .Name = DisplayString(manifest, L"name", 512),
            .Version = DisplayString(manifest, L"version", 64),
            .Resources = StringArray(manifest, L"resources"),
            .Types = StringArray(manifest, L"types"),
            .Catalogs = Catalogs(manifest),
            .Position = static_cast<std::int32_t>(position),
            .HideCatalogs = object.GetNamedBoolean(L"hideCatalogs", false),
            .IsGlobal = isGlobal,
            .ProvidedGlobally = !isGlobal && OptionalBoolean(object, L"providedGlobally", false),
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
            .Plan = OptionalBoundedText(object, L"plan", 64),
            .AvatarVersion = OptionalBoundedText(object, L"avatarVersion", 256),
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
        auto const updatedAt = ::HaloDesktop::Api::CheckedNonnegativeInt64(
            object.GetNamedNumber(L"updatedAt"));
        if (!updatedAt)
        {
            throw std::invalid_argument{ "The settings response has an invalid timestamp." };
        }
        return Dto::SettingsPayload{
            .Value = object.GetNamedObject(L"value"),
            .UpdatedAt = *updatedAt,
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
            auto preview = ParsePreview(object);
            // Validated first, so a malformed entry fails the page as it always
            // has; an action entry is then dropped without taking a slot.
            if (::HaloDesktop::Api::IsActionEntry(RawString(object, L"id"), RawString(object, L"name")))
            {
                continue;
            }
            preview.Genres = TolerantStringArray(object, L"genres", 32, 128);
            result.push_back(std::move(preview));
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
            if (object.HasKey(L"removedAt"))
            {
                auto const field = object.GetNamedValue(L"removedAt");
                if (field.ValueType() == winrt::Windows::Data::Json::JsonValueType::Number)
                {
                    removedAt = RequirePositiveInteger(object, L"removedAt");
                }
                else if (field.ValueType() != winrt::Windows::Data::Json::JsonValueType::Null)
                {
                    throw std::invalid_argument{ "The library row contains an invalid removal timestamp." };
                }
            }
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
            constexpr double MaximumWatchSeconds = static_cast<double>((std::numeric_limits<std::int32_t>::max)());
            if (!std::isfinite(position) || !std::isfinite(duration)
                || position < 0 || duration < 0
                || position > duration
                || position > MaximumWatchSeconds
                || duration > MaximumWatchSeconds)
            {
                continue;
            }
            std::optional<std::int64_t> removedAt;
            if (object.HasKey(L"removedAt")
                && object.GetNamedValue(L"removedAt").ValueType() == winrt::Windows::Data::Json::JsonValueType::Number)
            {
                removedAt = ::HaloDesktop::Api::CheckedPositiveInt64(object.GetNamedNumber(L"removedAt"));
            }
            result.push_back({
                DisplayString(object, L"videoId", 2048), DisplayString(object, L"itemId", 2048), position, duration,
                object.GetNamedBoolean(L"watched"), OptionalDisplayString(object, L"name", 512), OptionalHttpUrl(object, L"poster"),
                RequirePositiveInteger(object, L"updatedAt"), removedAt });
        }
        return result;
    }

    Dto::MetaDetail ParseMeta(winrt::Windows::Data::Json::IJsonValue const& value)
    {
        auto const root=RequireObject(value,L"The metadata response must be an object.");auto const object=root.GetNamedObject(L"meta");
        Dto::MetaDetail result;
        result.Preview=ParsePreview(object);
        result.Preview.Genres=OptionalStringArray(object,L"genres");
        result.Runtime=OptionalDisplayString(object,L"runtime",128);result.Cast=OptionalStringArray(object,L"cast");result.Director=OptionalStringArray(object,L"director");result.Writer=OptionalStringArray(object,L"writer");
        result.Released = OptionalBoundedText(object, L"released", 128);
        result.AgeRating = OptionalBoundedText(object, L"ageRating", 64);
        result.Country = OptionalBoundedText(object, L"country", 256);
        result.Language = OptionalBoundedText(object, L"language", 256);
        result.Awards = OptionalBoundedText(object, L"awards", 1024);
        if (auto const trailerStreams = OptionalArray(object, L"trailerStreams"))
        {
            for (auto const& item : *trailerStreams)
            {
                if (result.TrailerStreamIds.size() == 16) break;
                if (item.ValueType() != winrt::Windows::Data::Json::JsonValueType::Object) continue;
                if (auto const id = OptionalBoundedText(item.GetObject(), L"ytId", 64)) result.TrailerStreamIds.push_back(*id);
            }
        }
        if (auto const trailers = OptionalArray(object, L"trailers"))
        {
            for (auto const& item : *trailers)
            {
                if (result.Trailers.size() == 16) break;
                if (item.ValueType() != winrt::Windows::Data::Json::JsonValueType::Object) continue;
                auto const trailer = item.GetObject();
                if (auto const source = OptionalBoundedText(trailer, L"source", 64))
                {
                    result.Trailers.push_back({ *source, OptionalBoundedText(trailer, L"type", 64) });
                }
            }
        }
        // The reference client's cast rules: at most 40 people, names and parts
        // as bounded text, photos only as https URLs.
        if (auto const extras = OptionalObject(object, L"app_extras"))
        {
            if (auto const cast = OptionalArray(*extras, L"cast"))
            {
                for (auto const& item : *cast)
                {
                    if (result.PicturedCast.size() == 40) break;
                    if (item.ValueType() != winrt::Windows::Data::Json::JsonValueType::Object) continue;
                    auto const person = item.GetObject();
                    auto const name = OptionalBoundedText(person, L"name", 200);
                    if (!name) continue;
                    result.PicturedCast.push_back({
                        *name,
                        OptionalBoundedText(person, L"character", 200),
                        OptionalHttpsUrl(person, L"photo", 1000),
                    });
                }
            }
        }
        auto const videos = object.GetNamedValue(
            L"videos",
            winrt::Windows::Data::Json::JsonValue::CreateNullValue());
        if (videos.ValueType() != winrt::Windows::Data::Json::JsonValueType::Array)
        {
            return result;
        }
        for(auto const&item:videos.GetArray())
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
        // ParseStreamRecord drops a stream that would act rather than play,
        // which leaves the caller on the stream picker as a missing match does.
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

    std::optional<winrt::hstring> PlanRefusalMessage(winrt::hstring const& body)
    {
        winrt::Windows::Data::Json::JsonObject object{ nullptr };
        if (body.empty() || !winrt::Windows::Data::Json::JsonObject::TryParse(body, object) || !object)
        {
            return std::nullopt;
        }
        auto const code = RawString(object, L"code");
        if (code != L"plan_required" && code != L"plan_limit_reached")
        {
            return std::nullopt;
        }
        std::wstring message{ RawString(object, L"error") };
        std::replace_if(message.begin(), message.end(), [](wchar_t character) { return std::iswcntrl(character) != 0; }, L' ');
        std::wstring trimmed{ Trimmed(winrt::hstring{ message }) };
        if (trimmed.empty()) return std::nullopt;
        if (trimmed.size() > 512) trimmed.resize(512);
        return winrt::hstring{ trimmed };
    }
}
