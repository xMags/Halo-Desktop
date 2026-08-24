#pragma once

#include <cstdint>
#include <optional>
#include <pplawait.h>
#include <ppltasks.h>
#include <winrt/base.h>

namespace HaloDesktop::Services::Auth
{
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
    };
}
