#pragma once

#include <cstdint>
#include <optional>
#include <pplawait.h>
#include <ppltasks.h>
#include <winrt/base.h>

namespace HaloDesktop::Services::Auth
{
    // Which session a request is made for: the session generation and the
    // server that session talks to. ServerUrl is null for the built-in server;
    // only a username and password session opened on a self-hosted server
    // names another one. Two requests for the same target may share a token.
    struct SessionTarget final
    {
        std::uint64_t Generation{};
        std::optional<winrt::hstring> ServerUrl;

        friend bool operator==(SessionTarget const&, SessionTarget const&) = default;
    };

    // Thread-safe. A null refresh result means definitive session rejection;
    // transport and server failures throw without changing session state.
    class ITokenProvider
    {
    public:
        virtual ~ITokenProvider() = default;

        [[nodiscard]] virtual concurrency::task<std::optional<winrt::hstring>> AccessTokenAsync() = 0;
        [[nodiscard]] virtual concurrency::task<std::optional<winrt::hstring>> RefreshAccessTokenAsync() = 0;
        [[nodiscard]] virtual concurrency::task<void> RejectSessionAsync(std::uint64_t expectedGeneration) = 0;
        [[nodiscard]] virtual std::uint64_t SessionGeneration() const noexcept = 0;
        // The generation and server read together, so a request never pairs
        // one session's token with another session's server.
        [[nodiscard]] virtual SessionTarget CurrentTarget() const = 0;
    };
}
