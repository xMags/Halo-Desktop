#include "Playback/PlaybackPolicy.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace
{
    constexpr std::wstring_view ExternalSubtitleMarker=L"halo-subtitle:";

    std::wstring Lowercase(std::wstring value)
    {
        std::transform(value.begin(),value.end(),value.begin(),[](wchar_t character)
        {
            return static_cast<wchar_t>(std::towlower(character));
        });
        return value;
    }

    bool IsHeaderToken(std::wstring const& value) noexcept
    {
        if (value.empty())
        {
            return false;
        }
        constexpr std::wstring_view separators = L"()<>@,;:\\\"/[]?={} \t";
        return std::all_of(value.begin(), value.end(), [separators](wchar_t character)
        {
            return character > 31 && character < 127
                && separators.find(character) == std::wstring_view::npos;
        });
    }

    void AppendEscapedListValue(std::wstring& output, std::wstring const& value)
    {
        for (auto const character : value)
        {
            if (character == L'\\' || character == L',')
            {
                output.push_back(L'\\');
            }
            output.push_back(character);
        }
    }
}

namespace HaloDesktop::Playback
{
    std::wstring NormalizeLanguage(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(std::towlower(character));
        });
        auto const separator=value.find_first_of(L"-_");
        if(separator!=std::wstring::npos)value.resize(separator);
        if (value == L"en") return L"eng";
        if (value == L"ja") return L"jpn";
        if (value == L"es") return L"spa";
        if (value == L"fr") return L"fra";
        if (value == L"de") return L"deu";
        if (value == L"pt") return L"por";
        if (value == L"it") return L"ita";
        if (value == L"ru") return L"rus";
        if (value == L"zh") return L"zho";
        if (value == L"fre") return L"fra";
        if (value == L"ger") return L"deu";
        if (value == L"chi") return L"zho";
        if (value == L"cze") return L"ces";
        if (value == L"dut") return L"nld";
        if (value == L"gre") return L"ell";
        if (value == L"rum") return L"ron";
        if (value == L"slo") return L"slk";
        return value;
    }

    bool LanguageMatches(std::wstring const& left, std::wstring const& right)
    {
        return !left.empty() && !right.empty() && NormalizeLanguage(left) == NormalizeLanguage(right);
    }

    std::wstring LanguageDisplayName(std::wstring const&code)
    {
        static std::unordered_map<std::wstring,wchar_t const*>const labels{
            {L"eng",L"English"},{L"jpn",L"Japanese"},{L"spa",L"Spanish"},{L"fra",L"French"},
            {L"deu",L"German"},{L"por",L"Portuguese"},{L"pob",L"Portuguese (BR)"},{L"ita",L"Italian"},
            {L"rus",L"Russian"},{L"zho",L"Chinese"}};
        auto const found=labels.find(NormalizeLanguage(code));
        return found==labels.end()?code:std::wstring{found->second};
    }

    std::optional<std::int64_t> FindLanguageTrack(
        std::vector<TrackInfo> const& tracks,
        TrackType type,
        std::wstring const& language,
        bool embeddedOnly)
    {
        auto const found = std::find_if(tracks.begin(), tracks.end(), [type,embeddedOnly,&language](TrackInfo const& track)
        {
            return track.Type == type
                && (!embeddedOnly || !track.External)
                && LanguageMatches(track.Language, language);
        });
        return found == tracks.end() ? std::nullopt : std::optional<std::int64_t>{ found->Id };
    }

    std::wstring TrackSummary(std::vector<TrackInfo> const& tracks, TrackType type)
    {
        auto const found = std::find_if(tracks.begin(), tracks.end(), [type](TrackInfo const& track)
        {
            return track.Type == type && track.Selected;
        });
        if (found == tracks.end())
        {
            return type == TrackType::Subtitle ? L"Off" : L"Automatic";
        }

        auto result = found->Title;
        if (result.empty())
        {
            result = found->Language;
        }
        if (result.empty())
        {
            result = type == TrackType::Subtitle ? L"Subtitle" : L"Audio";
        }
        if (!found->Codec.empty())
        {
            result += L" \x00B7 " + found->Codec;
        }
        return result;
    }

    std::wstring EncodeExternalSubtitleTrackTitle(std::wstring const&identity,std::wstring const&displayTitle)
    {
        return std::wstring{ExternalSubtitleMarker}+std::to_wstring(identity.size())+L":"+identity+displayTitle;
    }

    std::optional<std::pair<std::wstring,std::wstring>> DecodeExternalSubtitleTrackTitle(std::wstring const&encoded)
    {
        if(!encoded.starts_with(ExternalSubtitleMarker))return std::nullopt;
        auto const lengthStart=ExternalSubtitleMarker.size();auto const separator=encoded.find(L':',lengthStart);
        if(separator==std::wstring::npos||separator==lengthStart)return std::nullopt;
        std::size_t length{};
        for(auto index=lengthStart;index<separator;++index)
        {
            auto const character=encoded[index];if(character<L'0'||character>L'9')return std::nullopt;
            auto const digit=static_cast<std::size_t>(character-L'0');
            if(length>((std::numeric_limits<std::size_t>::max)()-digit)/10)return std::nullopt;
            length=length*10+digit;
        }
        auto const identityStart=separator+1;if(length>encoded.size()-identityStart)return std::nullopt;
        return std::pair{encoded.substr(identityStart,length),encoded.substr(identityStart+length)};
    }

    bool CanApplyAutomaticSelection(std::uint64_t currentSelectionSerial,std::uint64_t initialSelectionSerial,std::uint64_t automaticSelectionSerial)noexcept
    {
        return currentSelectionSerial==initialSelectionSerial||currentSelectionSerial==automaticSelectionSerial;
    }

    bool ShouldApplyResume(
        bool resumeEnabled,
        bool watched,
        double positionSeconds,
        double durationSeconds,
        double currentPositionSeconds,
        std::uint64_t currentSeekSerial,
        std::uint64_t initialSeekSerial,
        bool withinStartupWindow) noexcept
    {
        if (!resumeEnabled || watched || positionSeconds <= 30 || durationSeconds <= 0.0
            || currentPositionSeconds > 5.0 || currentSeekSerial != initialSeekSerial || !withinStartupWindow)
        {
            return false;
        }
        return positionSeconds / durationSeconds < 0.95;
    }

    void ValidatePlaybackHeaders(std::vector<PlaybackHeader> const& headers)
    {
        if (headers.size() > 64)
        {
            throw std::invalid_argument{ "The source supplied too many request headers." };
        }
        constexpr std::array<std::wstring_view, 13> denied{
            L"connection", L"content-length", L"host", L"if-range", L"keep-alive",
            L"proxy-authenticate", L"proxy-authorization", L"proxy-connection", L"range",
            L"te", L"trailer", L"transfer-encoding", L"upgrade",
        };
        for (auto const& header : headers)
        {
            auto const lower = Lowercase(header.Name);
            if (header.Name.size() > 128 || header.Value.size() > 8192 || !IsHeaderToken(header.Name)
                || header.Value.find_first_of(L"\r\n") != std::wstring::npos
                || header.Value.find(L'\0') != std::wstring::npos
                || std::find(denied.begin(), denied.end(), lower) != denied.end())
            {
                throw std::invalid_argument{ "The source headers are not safe for playback." };
            }
        }
    }

    std::wstring SerializePlaybackHeaders(std::vector<PlaybackHeader> const& headers)
    {
        ValidatePlaybackHeaders(headers);
        std::wstring result;
        for (auto const& header : headers)
        {
            if (!result.empty())
            {
                result.push_back(L',');
            }
            AppendEscapedListValue(result, header.Name + L": " + header.Value);
        }
        return result;
    }
}
