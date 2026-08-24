#pragma once

#include <chrono>
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
}

namespace HaloDesktop::Api::Mappers
{
    [[nodiscard]] bool ParseHealth(winrt::Windows::Data::Json::IJsonValue const& value);
    [[nodiscard]] Dto::AuthConfig ParseAuthConfig(winrt::Windows::Data::Json::IJsonValue const& value);
    [[nodiscard]] Dto::IssuedToken ParseIssuedToken(winrt::Windows::Data::Json::IJsonValue const& value);
    [[nodiscard]] Dto::Me ParseMe(winrt::Windows::Data::Json::IJsonValue const& value);
}
