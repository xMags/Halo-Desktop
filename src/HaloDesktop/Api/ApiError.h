#pragma once

#include <cstdint>
#include <optional>
#include <winrt/base.h>

namespace HaloDesktop::Api::ApiError
{
    inline constexpr winrt::hresult Transport{ static_cast<std::int32_t>(0x8004A000u) };
    inline constexpr winrt::hresult SessionRejected{ static_cast<std::int32_t>(0x8004A001u) };

    inline constexpr std::uint32_t HttpStatusBase = 0x8004A100u;

    [[nodiscard]] constexpr winrt::hresult MakeHttpStatus(std::uint16_t status) noexcept
    {
        return winrt::hresult{ static_cast<std::int32_t>(HttpStatusBase + status) };
    }

    [[nodiscard]] constexpr bool IsTransport(winrt::hresult code) noexcept
    {
        return code.value == Transport.value;
    }

    [[nodiscard]] constexpr bool IsSessionRejected(winrt::hresult code) noexcept
    {
        return code.value == SessionRejected.value;
    }

    // A refusal by the account's plan (`plan_required`, `plan_limit_reached`).
    // It keeps its HTTP status, which HttpStatus still reports, and its
    // message is the server's own sentence, written to be shown as it is.
    inline constexpr std::uint32_t PlanRefusalBase = 0x8004A800u;

    [[nodiscard]] constexpr winrt::hresult MakePlanRefusal(std::uint16_t status) noexcept
    {
        return winrt::hresult{ static_cast<std::int32_t>(PlanRefusalBase + status) };
    }

    [[nodiscard]] constexpr bool IsPlanRefusal(winrt::hresult code) noexcept
    {
        auto const raw = static_cast<std::uint32_t>(code.value);
        return raw >= PlanRefusalBase + 100u && raw <= PlanRefusalBase + 599u;
    }

    [[nodiscard]] constexpr std::optional<std::uint16_t> HttpStatus(winrt::hresult code) noexcept
    {
        auto const raw = static_cast<std::uint32_t>(code.value);
        if (IsPlanRefusal(code))
        {
            return static_cast<std::uint16_t>(raw - PlanRefusalBase);
        }
        if (raw < HttpStatusBase + 100u || raw > HttpStatusBase + 599u)
        {
            return std::nullopt;
        }
        return static_cast<std::uint16_t>(raw - HttpStatusBase);
    }
}
