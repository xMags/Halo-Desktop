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
}
