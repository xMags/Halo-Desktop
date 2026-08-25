#pragma once

#include "Security/ProtectedHttpHeaders.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace HaloDesktop::Api
{
    constexpr std::uint64_t OpenSubtitlesHashChunkSize = 65'536;

    struct HashByteRange final
    {
        std::uint64_t Start{};
        std::uint64_t End{};

        bool operator==(HashByteRange const&) const = default;
    };

    struct HashContentRange final
    {
        std::uint64_t Start{};
        std::uint64_t End{};
        std::uint64_t Total{};

        bool operator==(HashContentRange const&) const = default;
    };

    [[nodiscard]] Security::ProtectedHttpHeaders BuildHashRequestHeaders(
        Security::ProtectedHttpHeaders const& protectedHeaders,
        std::optional<HashByteRange> range = std::nullopt);

    [[nodiscard]] std::uint64_t ValidateHashRangeResponse(
        std::uint16_t statusCode,
        std::optional<HashContentRange> contentRange,
        HashByteRange requestedRange,
        std::optional<std::uint64_t> expectedTotal,
        std::size_t bodyLength);

    [[nodiscard]] std::uint64_t ComputeOpenSubtitlesMovieHash(
        std::uint64_t size,
        std::vector<std::uint8_t> const& head,
        std::vector<std::uint8_t> const& tail);
}
