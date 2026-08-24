#pragma once

#include "Api/Dto.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace HaloDesktop::Services
{
    // Pure native parsing and ranking. No URL or request header is copied into
    // the display model produced from this structure.
    struct ParsedStreamInfo final
    {
        winrt::hstring Filename;
        std::optional<winrt::hstring> Quality;
        std::optional<winrt::hstring> DynamicRange;
        std::optional<winrt::hstring> Codec;
        std::optional<winrt::hstring> Audio;
        std::optional<std::uint64_t> SizeBytes;
        std::optional<bool> Cached;
        std::vector<winrt::hstring> Languages;
        winrt::hstring Detail;
    };

    [[nodiscard]] ParsedStreamInfo ParseStreamInfo(Api::Dto::StreamRecord const& stream);
    [[nodiscard]] int CompareStreams(ParsedStreamInfo const& left, ParsedStreamInfo const& right) noexcept;
    [[nodiscard]] winrt::hstring FormatStreamSize(std::optional<std::uint64_t> bytes);
    [[nodiscard]] winrt::hstring BuildSourceTagLine(ParsedStreamInfo const& info);
}
