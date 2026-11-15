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
    // A byte count as whole bytes: rounded to the nearest, and unknown when it is
    // zero, negative, not finite or past 2^64. A size read from text ("29.35 GB")
    // multiplies out to a fraction, and an addon can send one too; anything that
    // counts bytes, such as the download engine, refuses a fraction outright.
    [[nodiscard]] std::optional<std::uint64_t> WholeBytes(double value) noexcept;
    // Whether the parsed name came from the stream rather than from the placeholder
    // ParseStreamInfo falls back to. The placeholder is the same string for every
    // nameless source, so it identifies nothing and must never be used to decide
    // that two sources are the same file.
    [[nodiscard]] bool HasIdentifyingFilename(ParsedStreamInfo const& info) noexcept;
    // Best-first position of a parsed quality token (2160p, 1440p, 1080p, 720p,
    // 480p, SD); anything else, or none, ranks after all of them.
    [[nodiscard]] int QualityRank(std::optional<winrt::hstring> const& quality) noexcept;
    [[nodiscard]] int CompareStreams(ParsedStreamInfo const& left, ParsedStreamInfo const& right) noexcept;
    [[nodiscard]] winrt::hstring FormatStreamSize(std::optional<std::uint64_t> bytes);
}
