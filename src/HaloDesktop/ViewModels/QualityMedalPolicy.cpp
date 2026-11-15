#include "ViewModels/QualityMedalPolicy.h"

namespace
{
    using ::HaloDesktop::ViewModels::Medal;
    using ::HaloDesktop::ViewModels::MedalFinish;

    enum class Tier
    {
        UltraHd,
        FullHd,
        Hd,
        Lower,
    };

    Tier TierOf(std::wstring_view quality) noexcept
    {
        if (quality == L"2160p") return Tier::UltraHd;
        if (quality == L"1440p" || quality == L"1080p") return Tier::FullHd;
        if (quality == L"720p") return Tier::Hd;
        return Tier::Lower;
    }

    wchar_t const* TierTag(Tier tier) noexcept
    {
        switch (tier)
        {
        case Tier::UltraHd: return L"UHD";
        case Tier::FullHd: return L"FHD";
        case Tier::Hd: return L"HD";
        case Tier::Lower: break;
        }
        return L"";
    }

    MedalFinish FinishOf(Tier tier) noexcept
    {
        switch (tier)
        {
        case Tier::UltraHd: return MedalFinish::Gold;
        case Tier::FullHd: return MedalFinish::Silver;
        case Tier::Hd: return MedalFinish::Bronze;
        case Tier::Lower: break;
        }
        return MedalFinish::Plain;
    }

    bool IsHdr(std::wstring_view range) noexcept
    {
        return !range.empty() && range != L"SDR";
    }

    struct PictureTier final
    {
        std::int64_t MinimumPixels;
        wchar_t const* Plate;
        wchar_t const* Tag;
        MedalFinish Finish;
    };

    // The floors sit between neighbouring formats rather than on them, so a slightly
    // cropped master keeps its tier.
    constexpr PictureTier PictureTiers[]{
        { 5'500'000, L"4K", L"UHD", MedalFinish::Gold },
        { 3'000'000, L"1440p", L"QHD", MedalFinish::Silver },
        { 1'400'000, L"1080p", L"FHD", MedalFinish::Silver },
        { 600'000, L"720p", L"HD", MedalFinish::Bronze },
    };

    // The reference plate for an unparsed quality is an em dash. This repository does
    // not use that character anywhere, so the plate carries an en dash instead: the
    // same "nothing to report" mark at the plate's size.
    constexpr wchar_t UnknownPlate[] = L"\x2013";
}

namespace HaloDesktop::ViewModels
{
    Medal MedalFor(std::wstring_view quality, std::wstring_view range)
    {
        auto const tier = TierOf(quality);
        Medal medal;
        medal.Plate = quality == L"2160p" ? std::wstring{ L"4K" }
            : quality == NotParsedToken   ? std::wstring{ UnknownPlate }
                                          : std::wstring{ quality };
        if (range == L"DV")
        {
            medal.Tag = L"DV";
        }
        else if (IsHdr(range))
        {
            medal.Tag = L"HDR";
        }
        else if (quality == L"1440p")
        {
            medal.Tag = L"QHD";
        }
        else
        {
            medal.Tag = TierTag(tier);
        }
        medal.Finish = FinishOf(tier);
        return medal;
    }

    std::optional<Medal> MedalForPicture(Playback::VideoFormat const& format)
    {
        if (format.Width <= 0 || format.Height <= 0)
        {
            return std::nullopt;
        }
        auto const pixels = static_cast<std::int64_t>(format.Width) * static_cast<std::int64_t>(format.Height);
        Medal medal{ L"SD", {}, MedalFinish::Plain };
        for (auto const& tier : PictureTiers)
        {
            if (pixels >= tier.MinimumPixels)
            {
                medal = Medal{ tier.Plate, tier.Tag, tier.Finish };
                break;
            }
        }
        // Dolby Vision outranks the transfer: its base layer is usually PQ.
        switch (format.DynamicRange)
        {
        case Playback::VideoDynamicRange::DolbyVision:
            medal.Tag = L"DV";
            break;
        case Playback::VideoDynamicRange::Hdr:
            medal.Tag = L"HDR";
            break;
        case Playback::VideoDynamicRange::Hlg:
            medal.Tag = L"HLG";
            break;
        case Playback::VideoDynamicRange::Standard:
            break;
        }
        return medal;
    }
}
