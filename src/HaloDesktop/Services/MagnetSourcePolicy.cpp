#include "pch.h"
#include "Services/MagnetSourcePolicy.h"

#include "Services/StreamInfo.h"

#include <cstdint>
#include <string>

namespace
{
    namespace Dto = ::HaloDesktop::Api::Dto;

    // A whole decimal number, digits only, that fits an int32.
    std::optional<std::int32_t> Number(std::wstring_view text) noexcept
    {
        if (text.empty() || text.size() > 9) return std::nullopt;
        std::int32_t value{};
        for (auto const character : text)
        {
            if (character < L'0' || character > L'9') return std::nullopt;
            value = value * 10 + static_cast<std::int32_t>(character - L'0');
        }
        return value;
    }

    struct EpisodeRef final
    {
        std::int32_t Season{};
        std::int32_t Episode{};
    };

    // `<metaId>:<season>:<episode>`, exactly.
    std::optional<EpisodeRef> EpisodeOf(std::wstring_view videoId, std::wstring_view metaId) noexcept
    {
        if (metaId.empty() || videoId.size() <= metaId.size() + 1 || !videoId.starts_with(metaId) || videoId[metaId.size()] != L':')
        {
            return std::nullopt;
        }
        auto const rest = videoId.substr(metaId.size() + 1);
        auto const colon = rest.find(L':');
        if (colon == std::wstring_view::npos) return std::nullopt;
        auto const season = Number(rest.substr(0, colon));
        auto const episode = Number(rest.substr(colon + 1));
        if (!season || !episode) return std::nullopt;
        return EpisodeRef{ *season, *episode };
    }
}

namespace HaloDesktop::Services
{
    std::optional<std::size_t> LargestMagnetFile(std::span<Dto::MagnetFile const> files) noexcept
    {
        std::optional<std::size_t> best;
        for (std::size_t index = 0; index < files.size(); ++index)
        {
            if (!best || files[index].SizeBytes > files[*best].SizeBytes) best = index;
        }
        return best;
    }

    std::optional<Dto::MagnetFile> MagnetFileFor(Dto::MagnetRequest const& magnet, std::wstring_view type, std::wstring_view videoId)
    {
        if (magnet.State != Dto::MagnetState::Ready || !magnet.Identified || magnet.Files.empty())
        {
            return std::nullopt;
        }
        auto const& identity = *magnet.Identified;
        if (type != std::wstring_view{ identity.Type })
        {
            return std::nullopt;
        }
        if (type == L"movie")
        {
            if (videoId != std::wstring_view{ identity.Id }) return std::nullopt;
            auto const largest = LargestMagnetFile(magnet.Files);
            return largest ? std::optional<Dto::MagnetFile>{ magnet.Files[*largest] } : std::nullopt;
        }
        if (type != L"series")
        {
            return std::nullopt;
        }
        auto const wanted = EpisodeOf(videoId, std::wstring_view{ identity.Id });
        if (!wanted) return std::nullopt;
        std::optional<std::size_t> best;
        for (std::size_t index = 0; index < magnet.Files.size(); ++index)
        {
            auto const& file = magnet.Files[index];
            if (file.Season != wanted->Season || file.Episode != wanted->Episode) continue;
            if (!best || file.SizeBytes > magnet.Files[*best].SizeBytes) best = index;
        }
        return best ? std::optional<Dto::MagnetFile>{ magnet.Files[*best] } : std::nullopt;
    }

    MagnetSourceRow BuildMagnetSourceRow(Dto::MagnetFile const& file)
    {
        return MagnetSourceRow{
            .Name = MagnetSourceName,
            .FileName = file.Name,
            .Size = FormatStreamSize(file.SizeBytes > 0 ? std::optional<std::uint64_t>{ file.SizeBytes } : std::nullopt),
        };
    }
}
