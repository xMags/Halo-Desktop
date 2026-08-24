#include "pch.h"
#include "Services/Auth/OidcSignInFlow.h"

#include "Api/HttpExecutor.h"
#include "Services/Auth/LoopbackListener.h"

#include <array>
#include <bcrypt.h>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <wil/resource.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>

namespace
{
    constexpr wchar_t RedirectUri[] = L"http://127.0.0.1:17871/callback";
    constexpr std::size_t MaximumEndpointLength = 4096;
    constexpr std::size_t MaximumTokenLength = 65536;

    struct Discovery final
    {
        winrt::hstring AuthorizationEndpoint;
        winrt::hstring TokenEndpoint;
        std::optional<winrt::hstring> RevocationEndpoint;
        std::optional<winrt::hstring> EndSessionEndpoint;
    };

    struct TokenResponse final
    {
        winrt::hstring AccessToken;
        std::optional<winrt::hstring> RefreshToken;
        std::optional<winrt::hstring> IdToken;
        std::int64_t ExpiresAt{};
    };

    bool IsLoopback(winrt::hstring const& host)
    {
        return host == L"127.0.0.1" || host == L"localhost" || host == L"::1";
    }

    void RequireSecureEndpoint(winrt::hstring const& endpoint)
    {
        if (endpoint.empty() || endpoint.size() > MaximumEndpointLength)
        {
            throw std::invalid_argument{ "The OIDC endpoint length is invalid." };
        }
        winrt::Windows::Foundation::Uri const uri{ endpoint };
        if (uri.Host().empty()
            || (uri.SchemeName() != L"https"
                && !(uri.SchemeName() == L"http" && IsLoopback(uri.Host()))))
        {
            throw std::invalid_argument{ "OIDC endpoints must use HTTPS except on loopback." };
        }
    }

    std::optional<winrt::hstring> OptionalEndpoint(
        winrt::Windows::Data::Json::JsonObject const& object,
        wchar_t const* name)
    {
        if (!object.HasKey(name))
        {
            return std::nullopt;
        }
        auto const value = object.GetNamedString(name);
        if (value.empty())
        {
            return std::nullopt;
        }
        RequireSecureEndpoint(value);
        return value;
    }

    Discovery ParseDiscovery(winrt::Windows::Data::Json::IJsonValue const& value)
    {
        auto const object = value.GetObject();
        auto const authorization = object.GetNamedString(L"authorization_endpoint");
        auto const token = object.GetNamedString(L"token_endpoint");
        if (authorization.empty() || token.empty())
        {
            throw std::invalid_argument{ "OIDC discovery is missing required endpoints." };
        }
        RequireSecureEndpoint(authorization);
        RequireSecureEndpoint(token);
        return Discovery{
            .AuthorizationEndpoint = authorization,
            .TokenEndpoint = token,
            .RevocationEndpoint = OptionalEndpoint(object, L"revocation_endpoint"),
            .EndSessionEndpoint = OptionalEndpoint(object, L"end_session_endpoint"),
        };
    }

    std::string Base64Url(std::span<std::uint8_t const> bytes)
    {
        constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
        std::string result;
        result.reserve((bytes.size() * 4 + 2) / 3);
        std::size_t index{};
        while (index + 3 <= bytes.size())
        {
            auto const value = (static_cast<std::uint32_t>(bytes[index]) << 16)
                | (static_cast<std::uint32_t>(bytes[index + 1]) << 8)
                | static_cast<std::uint32_t>(bytes[index + 2]);
            result.push_back(alphabet[(value >> 18) & 63]);
            result.push_back(alphabet[(value >> 12) & 63]);
            result.push_back(alphabet[(value >> 6) & 63]);
            result.push_back(alphabet[value & 63]);
            index += 3;
        }
        auto const remaining = bytes.size() - index;
        if (remaining == 1)
        {
            auto const value = static_cast<std::uint32_t>(bytes[index]) << 16;
            result.push_back(alphabet[(value >> 18) & 63]);
            result.push_back(alphabet[(value >> 12) & 63]);
        }
        else if (remaining == 2)
        {
            auto const value = (static_cast<std::uint32_t>(bytes[index]) << 16)
                | (static_cast<std::uint32_t>(bytes[index + 1]) << 8);
            result.push_back(alphabet[(value >> 18) & 63]);
            result.push_back(alphabet[(value >> 12) & 63]);
            result.push_back(alphabet[(value >> 6) & 63]);
        }
        return result;
    }

    winrt::hstring RandomToken()
    {
        std::array<std::uint8_t, 32> bytes{};
        winrt::check_nt(BCryptGenRandom(
            nullptr,
            bytes.data(),
            static_cast<ULONG>(bytes.size()),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG));
        return winrt::to_hstring(Base64Url(bytes));
    }

    winrt::hstring PkceChallenge(winrt::hstring const& verifier)
    {
        auto const utf8 = winrt::to_string(verifier);
        std::vector<std::uint8_t> verifierBytes(utf8.begin(), utf8.end());
        auto wipeVerifier = wil::scope_exit([&verifierBytes]() noexcept
        {
            if (!verifierBytes.empty())
            {
                SecureZeroMemory(verifierBytes.data(), verifierBytes.size());
            }
        });
        BCRYPT_ALG_HANDLE rawAlgorithm{};
        winrt::check_nt(BCryptOpenAlgorithmProvider(
            &rawAlgorithm,
            BCRYPT_SHA256_ALGORITHM,
            nullptr,
            0));
        auto closeAlgorithm = wil::scope_exit([&rawAlgorithm]() noexcept
        {
            BCryptCloseAlgorithmProvider(rawAlgorithm, 0);
        });

        BCRYPT_HASH_HANDLE rawHash{};
        winrt::check_nt(BCryptCreateHash(rawAlgorithm, &rawHash, nullptr, 0, nullptr, 0, 0));
        auto closeHash = wil::scope_exit([&rawHash]() noexcept
        {
            BCryptDestroyHash(rawHash);
        });
        winrt::check_nt(BCryptHashData(
            rawHash,
            verifierBytes.data(),
            static_cast<ULONG>(verifierBytes.size()),
            0));
        std::array<std::uint8_t, 32> digest{};
        winrt::check_nt(BCryptFinishHash(rawHash, digest.data(), static_cast<ULONG>(digest.size()), 0));
        return winrt::to_hstring(Base64Url(digest));
    }

    bool IsUnreserved(std::uint8_t byte)
    {
        return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z')
            || (byte >= '0' && byte <= '9') || byte == '-' || byte == '_'
            || byte == '.' || byte == '!' || byte == '~' || byte == '*'
            || byte == '\'' || byte == '(' || byte == ')';
    }

    winrt::hstring Encode(winrt::hstring const& input)
    {
        constexpr char hex[] = "0123456789ABCDEF";
        auto const utf8 = winrt::to_string(input);
        std::string result;
        for (auto const character : utf8)
        {
            auto const byte = static_cast<std::uint8_t>(character);
            if (IsUnreserved(byte))
            {
                result.push_back(static_cast<char>(byte));
            }
            else
            {
                result.push_back('%');
                result.push_back(hex[byte >> 4]);
                result.push_back(hex[byte & 15]);
            }
        }
        return winrt::to_hstring(result);
    }

    winrt::hstring JoinPairs(std::vector<std::pair<winrt::hstring, winrt::hstring>> const& pairs)
    {
        std::wstring result;
        for (auto const& [name, value] : pairs)
        {
            if (!result.empty())
            {
                result.push_back(L'&');
            }
            result.append(Encode(name));
            result.push_back(L'=');
            result.append(Encode(value));
        }
        return winrt::hstring{ result };
    }

    std::optional<winrt::hstring> CallbackValue(winrt::hstring const& path, wchar_t const* name)
    {
        std::wstring text{ path };
        auto const query = text.find(L'?');
        if (query == std::wstring::npos || query + 1 >= text.size())
        {
            return std::nullopt;
        }
        winrt::Windows::Foundation::WwwFormUrlDecoder decoder{ winrt::hstring{ text.substr(query + 1) } };
        for (auto const& entry : decoder)
        {
            if (entry.Name() == name)
            {
                return entry.Value();
            }
        }
        return std::nullopt;
    }

    TokenResponse ParseTokenResponse(
        winrt::Windows::Web::Http::HttpResponseMessage const& response,
        winrt::hstring const& body)
    {
        auto const object = winrt::Windows::Data::Json::JsonObject::Parse(body);
        if (!response.IsSuccessStatusCode() || object.HasKey(L"error"))
        {
            throw std::runtime_error{ "The identity provider rejected the token exchange." };
        }
        auto const accessToken = object.GetNamedString(L"access_token");
        if (accessToken.empty() || accessToken.size() > MaximumTokenLength)
        {
            throw std::invalid_argument{ "The identity provider returned no access token." };
        }

        std::int64_t expiresAt{};
        if (object.HasKey(L"expires_in"))
        {
            auto const seconds = object.GetNamedNumber(L"expires_in");
            if (!std::isfinite(seconds) || seconds <= 0
                || seconds > static_cast<double>((std::numeric_limits<std::int32_t>::max)()))
            {
                throw std::invalid_argument{ "The identity provider returned an invalid token lifetime." };
            }
            expiresAt = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()
                + static_cast<std::int64_t>(seconds * 1000.0);
        }
        auto optionalString = [&object](wchar_t const* name) -> std::optional<winrt::hstring>
        {
            if (!object.HasKey(name))
            {
                return std::nullopt;
            }
            auto const value = object.GetNamedString(name);
            if (value.empty())
            {
                return std::nullopt;
            }
            if (value.size() > MaximumTokenLength)
            {
                throw std::invalid_argument{ "The identity provider returned an oversized token." };
            }
            return value;
        };
        return TokenResponse{
            .AccessToken = accessToken,
            .RefreshToken = optionalString(L"refresh_token"),
            .IdToken = optionalString(L"id_token"),
            .ExpiresAt = expiresAt,
        };
    }
}

namespace HaloDesktop::Services::Auth
{
    OidcSignInFlow::OidcSignInFlow(std::shared_ptr<::HaloDesktop::Api::HttpExecutor> executor)
        : m_executor(std::move(executor))
    {
        if (!m_executor)
        {
            throw std::invalid_argument{ "OidcSignInFlow requires an HTTP executor." };
        }
    }

    concurrency::task<OidcSignInResult> OidcSignInFlow::SignInAsync(
        ::HaloDesktop::Api::Dto::AuthConfig config)
    {
        co_await winrt::resume_background();
        if (config.Mode != ::HaloDesktop::Api::Dto::AuthMode::Oidc
            || config.Issuer.empty() || config.ClientId.empty())
        {
            co_return OidcSignInResult{ winrt::HaloDesktop::SignInOutcome::Unreachable };
        }

        try
        {
            RequireSecureEndpoint(config.Issuer);
            auto discoveryUrl = std::wstring{ config.Issuer };
            if (discoveryUrl.back() != L'/')
            {
                discoveryUrl.push_back(L'/');
            }
            discoveryUrl.append(L".well-known/openid-configuration");
            auto const discoveryValue = co_await m_executor->SendJsonAsync(
                winrt::Windows::Web::Http::HttpMethod::Get(),
                winrt::Windows::Foundation::Uri{ discoveryUrl });
            auto const discovery = ParseDiscovery(discoveryValue);

            auto const state = RandomToken();
            auto const verifier = RandomToken();
            std::wstring scopes;
            for (auto const& scope : config.Scopes)
            {
                if (!scopes.empty())
                {
                    scopes.push_back(L' ');
                }
                scopes.append(scope);
            }
            auto const query = JoinPairs({
                { L"response_type", L"code" },
                { L"client_id", config.ClientId },
                { L"redirect_uri", RedirectUri },
                { L"scope", winrt::hstring{ scopes } },
                { L"state", state },
                { L"code_challenge", PkceChallenge(verifier) },
                { L"code_challenge_method", L"S256" },
            });
            auto const separator = std::wstring{ discovery.AuthorizationEndpoint }.find(L'?') == std::wstring::npos
                ? L"?"
                : L"&";
            auto const authorizationUrl = discovery.AuthorizationEndpoint + separator + query;

            LoopbackListener listener;
            auto callbackTask = listener.WaitAsync();
            auto const launched = co_await winrt::Windows::System::Launcher::LaunchUriAsync(
                winrt::Windows::Foundation::Uri{ authorizationUrl });
            if (!launched)
            {
                listener.Cancel();
                try
                {
                    static_cast<void>(co_await callbackTask);
                }
                catch (...)
                {
                }
                co_return OidcSignInResult{ winrt::HaloDesktop::SignInOutcome::Unreachable };
            }
            auto const callbackPath = co_await callbackTask;
            if (CallbackValue(callbackPath, L"error"))
            {
                co_return OidcSignInResult{ winrt::HaloDesktop::SignInOutcome::Declined };
            }
            auto const code = CallbackValue(callbackPath, L"code");
            auto const returnedState = CallbackValue(callbackPath, L"state");
            if (!code || !returnedState || *returnedState != state)
            {
                co_return OidcSignInResult{ winrt::HaloDesktop::SignInOutcome::Declined };
            }

            auto const form = JoinPairs({
                { L"grant_type", L"authorization_code" },
                { L"client_id", config.ClientId },
                { L"code", *code },
                { L"redirect_uri", RedirectUri },
                { L"code_verifier", verifier },
            });
            auto const tokenResponse = co_await m_executor->SendFormWithoutRedirectAsync(
                winrt::Windows::Foundation::Uri{ discovery.TokenEndpoint },
                form);
            auto const tokenBody = co_await tokenResponse.Content().ReadAsStringAsync();
            auto const tokens = ParseTokenResponse(tokenResponse, tokenBody);
            co_return OidcSignInResult{
                .Outcome = winrt::HaloDesktop::SignInOutcome::Succeeded,
                .Session = StoredOidcSession{
                    .ClientId = config.ClientId,
                    .TokenEndpoint = discovery.TokenEndpoint,
                    .RevocationEndpoint = discovery.RevocationEndpoint,
                    .EndSessionEndpoint = discovery.EndSessionEndpoint,
                    .AccessToken = tokens.AccessToken,
                    .RefreshToken = tokens.RefreshToken,
                    .IdToken = tokens.IdToken,
                    .ExpiresAt = tokens.ExpiresAt,
                },
            };
        }
        catch (...)
        {
            co_return OidcSignInResult{ winrt::HaloDesktop::SignInOutcome::Unreachable };
        }
    }
}
