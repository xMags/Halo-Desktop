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
        winrt::hstring Id;
        std::optional<winrt::hstring> TransportUrl;
        winrt::hstring Name;
        winrt::hstring Version;
        std::vector<winrt::hstring> Resources;
        std::vector<winrt::hstring> Types;
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
}

namespace HaloDesktop::Api::Mappers
{
    [[nodiscard]] bool ParseHealth(winrt::Windows::Data::Json::IJsonValue const& value);
    [[nodiscard]] Dto::AuthConfig ParseAuthConfig(winrt::Windows::Data::Json::IJsonValue const& value);
    [[nodiscard]] Dto::IssuedToken ParseIssuedToken(winrt::Windows::Data::Json::IJsonValue const& value);
    [[nodiscard]] Dto::Me ParseMe(winrt::Windows::Data::Json::IJsonValue const& value);
    [[nodiscard]] Dto::AddonsPayload ParseAddons(winrt::Windows::Data::Json::IJsonValue const& value);
    [[nodiscard]] Dto::SettingsPayload ParseSettings(winrt::Windows::Data::Json::IJsonValue const& value);
}
