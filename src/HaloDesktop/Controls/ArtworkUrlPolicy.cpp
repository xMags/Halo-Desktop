#include "Controls/ArtworkUrlPolicy.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace
{
    constexpr std::wstring_view TmdbImagePrefix = L"https://image.tmdb.org/t/p/";
    constexpr std::wstring_view TmdbOriginalSegment = L"original/";
    // TMDB's profile sizes are w45, w185, h632 and original.
    constexpr std::wstring_view TmdbThumbnailProfileSegment = L"w185/";
    constexpr std::wstring_view TmdbLargeProfileSegment = L"h632/";
    constexpr std::wstring_view MetahubPosterPrefix = L"https://images.metahub.space/poster/medium/";
    constexpr std::wstring_view MetahubLogoPrefix = L"https://images.metahub.space/logo/medium/";
    constexpr std::wstring_view MetahubPosterSuffix = L"/img";

    // /^tt\d+$/
    bool IsImdbId(std::wstring_view id) noexcept
    {
        if (id.size() < 3 || id[0] != L't' || id[1] != L't') return false;
        return std::all_of(id.begin() + 2, id.end(), [](wchar_t character)
        {
            return character >= L'0' && character <= L'9';
        });
    }
}

namespace HaloDesktop::Controls
{
    std::wstring CardArtworkUrl(std::wstring_view url, int width)
    {
        if (!url.starts_with(TmdbImagePrefix)) return std::wstring{ url };
        auto const rest = url.substr(TmdbImagePrefix.size());
        if (!rest.starts_with(TmdbOriginalSegment)) return std::wstring{ url };
        std::wstring result{ TmdbImagePrefix };
        result.push_back(L'w');
        result.append(std::to_wstring(width));
        result.push_back(L'/');
        result.append(rest.substr(TmdbOriginalSegment.size()));
        return result;
    }

    std::wstring PortraitPhotoUrl(std::wstring_view url)
    {
        if (!url.starts_with(TmdbImagePrefix)) return std::wstring{ url };
        auto const rest = url.substr(TmdbImagePrefix.size());
        if (!rest.starts_with(TmdbThumbnailProfileSegment)) return std::wstring{ url };
        std::wstring result{ TmdbImagePrefix };
        result.append(TmdbLargeProfileSegment);
        result.append(rest.substr(TmdbThumbnailProfileSegment.size()));
        return result;
    }

    std::optional<std::wstring> MetahubLogoUrl(std::wstring_view id)
    {
        if (!IsImdbId(id)) return std::nullopt;
        std::wstring result{ MetahubLogoPrefix };
        result.append(id);
        result.append(MetahubPosterSuffix);
        return result;
    }

    std::wstring FileImageUrl(std::filesystem::path const& path)
    {
        if (path.empty() || !path.is_absolute()) return {};
        constexpr wchar_t hex[] = L"0123456789ABCDEF";
        auto const text = path.generic_wstring();
        std::wstring result{ L"file:///" };
        auto pushByte = [&result, &hex](std::uint32_t byte)
        {
            result.push_back(L'%');
            result.push_back(hex[(byte >> 4) & 0x0F]);
            result.push_back(hex[byte & 0x0F]);
        };
        for (std::size_t index = 0; index < text.size(); ++index)
        {
            auto const character = text[index];
            auto const plain = (character >= L'a' && character <= L'z')
                || (character >= L'A' && character <= L'Z')
                || (character >= L'0' && character <= L'9')
                || character == L'/' || character == L':' || character == L'-'
                || character == L'_' || character == L'.' || character == L'~';
            if (plain)
            {
                result.push_back(character);
                continue;
            }
            // The code point, joining a surrogate pair; a lone half cannot name a file.
            std::uint32_t point = character;
            if (point >= 0xD800 && point <= 0xDBFF)
            {
                if (index + 1 >= text.size()) return {};
                std::uint32_t const low = text[index + 1];
                if (low < 0xDC00 || low > 0xDFFF) return {};
                point = 0x10000 + ((point - 0xD800) << 10) + (low - 0xDC00);
                ++index;
            }
            else if (point >= 0xDC00 && point <= 0xDFFF)
            {
                return {};
            }
            // Percent-encoded as UTF-8, so a title in any script still loads.
            if (point < 0x80)
            {
                pushByte(point);
            }
            else if (point < 0x800)
            {
                pushByte(0xC0 | (point >> 6));
                pushByte(0x80 | (point & 0x3F));
            }
            else if (point < 0x10000)
            {
                pushByte(0xE0 | (point >> 12));
                pushByte(0x80 | ((point >> 6) & 0x3F));
                pushByte(0x80 | (point & 0x3F));
            }
            else
            {
                pushByte(0xF0 | (point >> 18));
                pushByte(0x80 | ((point >> 12) & 0x3F));
                pushByte(0x80 | ((point >> 6) & 0x3F));
                pushByte(0x80 | (point & 0x3F));
            }
        }
        return result;
    }

    std::optional<std::wstring> MetahubPosterUrl(std::wstring_view id)
    {
        if (!IsImdbId(id)) return std::nullopt;
        std::wstring result{ MetahubPosterPrefix };
        result.append(id);
        result.append(MetahubPosterSuffix);
        return result;
    }

    std::vector<std::wstring> ArtworkCandidates(
        std::wstring_view source,
        std::wstring_view fallback,
        std::wstring_view metaId)
    {
        std::vector<std::wstring> result;
        auto const add = [&result](std::wstring_view url)
        {
            if (url.empty() || std::find(result.begin(), result.end(), url) != result.end()) return;
            result.emplace_back(url);
        };
        add(source);
        add(fallback);
        if (auto const poster = MetahubPosterUrl(metaId)) add(*poster);
        return result;
    }

    std::optional<std::chrono::milliseconds> ArtworkRetryDelay(int retriesDone) noexcept
    {
        switch (retriesDone)
        {
        case 0: return std::chrono::milliseconds{ 2'000 };
        case 1: return std::chrono::milliseconds{ 8'000 };
        case 2: return std::chrono::milliseconds{ 30'000 };
        default: return std::nullopt;
        }
    }

    bool UsesCardArtwork(double decodeWidth) noexcept
    {
        return decodeWidth > 0.0 && decodeWidth <= static_cast<double>(CardArtworkWidth);
    }
}
