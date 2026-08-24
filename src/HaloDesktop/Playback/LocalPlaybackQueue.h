#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string_view>

namespace HaloDesktop::Playback
{
    class LocalPlaybackQueue final
    {
    public:
        using ExtensionList = std::array<std::wstring_view, 6>;

        [[nodiscard]] static ExtensionList const& SupportedExtensions() noexcept;
        [[nodiscard]] static std::optional<std::filesystem::path>
        NextAfter(std::filesystem::path const& currentSource);
    };
} // namespace HaloDesktop::Playback
