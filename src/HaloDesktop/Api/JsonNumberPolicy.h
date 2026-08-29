#pragma once

#include <cstdint>
#include <optional>

namespace HaloDesktop::Api
{
    // JSON numbers are represented as doubles by Windows.Data.Json. The
    // exclusive ceiling is 2^63 because that value is not representable as an
    // int64_t, even though converting INT64_MAX to double rounds up to it.
    [[nodiscard]] std::optional<std::int64_t> CheckedPositiveInt64(double value) noexcept;
    [[nodiscard]] std::optional<std::int64_t> CheckedNonnegativeInt64(double value) noexcept;
    [[nodiscard]] std::optional<std::int64_t> CheckedTokenExpiry(
        double seconds,
        std::int64_t nowMilliseconds) noexcept;
}
