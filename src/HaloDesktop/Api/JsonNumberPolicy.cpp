#include "pch.h"
#include "Api/JsonNumberPolicy.h"

#include <cmath>
#include <limits>

namespace HaloDesktop::Api
{
    namespace
    {
        constexpr double Int64ExclusiveUpperBound = 9'223'372'036'854'775'808.0;

        std::optional<std::int64_t> CheckedInt64(double value, bool positive) noexcept
        {
            if (!std::isfinite(value)
                || (positive ? value <= 0.0 : value < 0.0)
                || value >= Int64ExclusiveUpperBound
                || std::floor(value) != value)
            {
                return std::nullopt;
            }
            return static_cast<std::int64_t>(value);
        }
    }

    std::optional<std::int64_t> CheckedPositiveInt64(double value) noexcept
    {
        return CheckedInt64(value, true);
    }

    std::optional<std::int64_t> CheckedNonnegativeInt64(double value) noexcept
    {
        return CheckedInt64(value, false);
    }

    std::optional<std::int64_t> CheckedTokenExpiry(
        double seconds,
        std::int64_t nowMilliseconds) noexcept
    {
        constexpr double MaximumTokenLifetimeSeconds =
            static_cast<double>((std::numeric_limits<std::int32_t>::max)());
        if (!std::isfinite(seconds)
            || seconds <= 0.0
            || std::floor(seconds) != seconds
            || seconds > MaximumTokenLifetimeSeconds)
        {
            return std::nullopt;
        }
        auto const lifetimeMilliseconds = static_cast<std::int64_t>(seconds * 1000.0);
        if (nowMilliseconds > (std::numeric_limits<std::int64_t>::max)() - lifetimeMilliseconds)
        {
            return std::nullopt;
        }
        return nowMilliseconds + lifetimeMilliseconds;
    }
}
