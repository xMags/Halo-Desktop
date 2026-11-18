#include "Services/BuildInfoPolicy.h"

#include <cctype>

namespace
{
    constexpr std::uint32_t FixedFileInfoSignature = 0xFEEF04BDu;
    // dwSignature, dwStrucVersion, dwFileVersionMS, dwFileVersionLS.
    constexpr std::size_t FixedFileInfoPrefixBytes = 16;

    std::uint32_t ReadUInt32(std::span<std::uint8_t const> bytes, std::size_t offset) noexcept
    {
        return static_cast<std::uint32_t>(bytes[offset])
            | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8)
            | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16)
            | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
    }

    bool IsDigit(char character) noexcept
    {
        return character >= '0' && character <= '9';
    }

    // Reads `\d+` at `position`, returning where it ended, or nothing.
    std::optional<std::size_t> Digits(std::string_view text, std::size_t position) noexcept
    {
        auto end = position;
        while (end < text.size() && IsDigit(text[end]))
        {
            ++end;
        }
        return end > position ? std::optional<std::size_t>{ end } : std::nullopt;
    }

    std::wstring Widen(std::string_view text)
    {
        // The release number is ASCII; other bytes of a raw string are kept
        // one for one, which is all a version label needs.
        std::wstring result;
        result.reserve(text.size());
        for (auto const character : text)
        {
            result.push_back(static_cast<wchar_t>(static_cast<unsigned char>(character)));
        }
        return result;
    }

    bool StartsWithMpvWord(std::string_view text, std::size_t& prefixLength) noexcept
    {
        if (text.size() < 3)
        {
            return false;
        }
        auto const lower = [](char character)
        {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        };
        if (lower(text[0]) != 'm' || lower(text[1]) != 'p' || lower(text[2]) != 'v')
        {
            return false;
        }
        auto end = std::size_t{ 3 };
        if (end >= text.size() || std::isspace(static_cast<unsigned char>(text[end])) == 0)
        {
            return false;
        }
        while (end < text.size() && std::isspace(static_cast<unsigned char>(text[end])) != 0)
        {
            ++end;
        }
        prefixLength = end;
        return true;
    }
}

namespace HaloDesktop::Services
{
    std::optional<std::wstring> ParseFixedFileVersion(std::span<std::uint8_t const> resource)
    {
        // The fixed-info record sits 32-bit aligned inside the resource, after
        // the header and the "VS_VERSION_INFO" key.
        for (std::size_t offset = 0; offset + FixedFileInfoPrefixBytes <= resource.size(); offset += 4)
        {
            if (ReadUInt32(resource, offset) != FixedFileInfoSignature)
            {
                continue;
            }
            auto const most = ReadUInt32(resource, offset + 8);
            auto const least = ReadUInt32(resource, offset + 12);
            auto version = std::to_wstring(most >> 16) + L"." + std::to_wstring(most & 0xFFFFu)
                + L"." + std::to_wstring(least >> 16);
            if ((least & 0xFFFFu) != 0)
            {
                version += L"." + std::to_wstring(least & 0xFFFFu);
            }
            return version;
        }
        return std::nullopt;
    }

    std::optional<std::wstring> MpvRelease(std::optional<std::string_view> raw)
    {
        if (!raw || raw->empty())
        {
            return std::nullopt;
        }
        auto const text = *raw;
        // The first `v?\d+\.\d+\.\d+`, as the reference's expression finds it.
        for (std::size_t start = 0; start < text.size(); ++start)
        {
            auto const first = Digits(text, start);
            if (!first || (start > 0 && IsDigit(text[start - 1])))
            {
                continue;
            }
            if (*first >= text.size() || text[*first] != '.')
            {
                continue;
            }
            auto const second = Digits(text, *first + 1);
            if (!second || *second >= text.size() || text[*second] != '.')
            {
                continue;
            }
            auto const third = Digits(text, *second + 1);
            if (!third)
            {
                continue;
            }
            return Widen(text.substr(start, *third - start));
        }
        std::size_t prefix{};
        if (StartsWithMpvWord(text, prefix))
        {
            return Widen(text.substr(prefix));
        }
        return Widen(text);
    }

    std::wstring BuildInfoLine(BuildInfoLineInput const& input)
    {
        auto const part = [&input](std::optional<std::wstring> const& value) -> std::wstring
        {
            if (!input.Loaded)
            {
                return L"…";
            }
            return value && !value->empty() ? *value : std::wstring{ L"unknown" };
        };
        return L"v" + part(input.App) + L" · mpv " + part(input.Mpv);
    }
}
