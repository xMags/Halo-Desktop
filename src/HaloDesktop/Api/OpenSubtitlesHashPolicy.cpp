#include "Api/OpenSubtitlesHashPolicy.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace
{
    std::uint64_t SumWords(std::vector<std::uint8_t> const& bytes) noexcept
    {
        std::uint64_t sum{};
        for (std::size_t offset = 0; offset < bytes.size(); offset += 8)
        {
            std::uint64_t word{};
            auto const count = (std::min)(std::size_t{ 8 }, bytes.size() - offset);
            for (std::size_t index = 0; index < count; ++index)
            {
                word |= static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8);
            }
            sum += word;
        }
        return sum;
    }
}

namespace HaloDesktop::Api
{
    Security::ProtectedHttpHeaders BuildHashRequestHeaders(
        Security::ProtectedHttpHeaders const& protectedHeaders,
        std::optional<HashByteRange> range)
    {
        Security::ValidateProtectedHttpHeaders(protectedHeaders);
        auto result = protectedHeaders;
        if (range)
        {
            if (range->End < range->Start)
            {
                throw std::invalid_argument{ "The hash byte range is invalid." };
            }
            result.push_back({
                L"Range",
                L"bytes=" + std::to_wstring(range->Start) + L"-" + std::to_wstring(range->End),
            });
        }
        return result;
    }

    std::uint64_t ValidateHashRangeResponse(
        std::uint16_t statusCode,
        std::optional<HashContentRange> contentRange,
        HashByteRange requestedRange,
        std::optional<std::uint64_t> expectedTotal,
        std::size_t bodyLength)
    {
        if (statusCode != 206)
        {
            throw std::runtime_error{ "The source ignored a range request." };
        }
        if (!contentRange || contentRange->Start != requestedRange.Start
            || contentRange->End != requestedRange.End || contentRange->Total == 0
            || contentRange->End >= contentRange->Total)
        {
            throw std::runtime_error{ "The source returned an inconsistent content range." };
        }
        if (expectedTotal && contentRange->Total != *expectedTotal)
        {
            throw std::runtime_error{ "The source size changed while hashing." };
        }

        auto const expectedLength = requestedRange.End - requestedRange.Start + 1;
        if (expectedLength > (std::numeric_limits<std::size_t>::max)()
            || bodyLength != static_cast<std::size_t>(expectedLength))
        {
            throw std::runtime_error{ "The source returned an incomplete hash range." };
        }
        return contentRange->Total;
    }

    std::uint64_t ComputeOpenSubtitlesMovieHash(
        std::uint64_t size,
        std::vector<std::uint8_t> const& head,
        std::vector<std::uint8_t> const& tail)
    {
        if (size < OpenSubtitlesHashChunkSize * 2
            || head.size() != OpenSubtitlesHashChunkSize
            || tail.size() != OpenSubtitlesHashChunkSize)
        {
            throw std::invalid_argument{ "Moviehash requires exact head and tail chunks." };
        }
        return size + SumWords(head) + SumWords(tail);
    }
}
