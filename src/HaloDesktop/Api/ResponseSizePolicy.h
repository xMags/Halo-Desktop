#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace HaloDesktop::Api
{
    inline constexpr std::size_t MaximumJsonResponseBytes = 16u * 1024u * 1024u;

    void ValidateDeclaredResponseSize(
        std::optional<std::uint64_t> declaredBytes,
        std::size_t maximumBytes = MaximumJsonResponseBytes);

    [[nodiscard]] std::size_t CheckedResponseSize(
        std::size_t currentBytes,
        std::size_t incomingBytes,
        std::size_t maximumBytes = MaximumJsonResponseBytes);
}
