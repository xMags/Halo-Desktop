#include "pch.h"
#include "Api/Dto.h"

#include <algorithm>
#include <cwctype>
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
        if (value.empty())
        {
            throw std::invalid_argument{ "The server returned an empty required field." };
        }
        return value;
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
}
