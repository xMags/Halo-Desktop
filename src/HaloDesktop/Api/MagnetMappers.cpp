#include "pch.h"
#include "Api/Dto.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>

// A pasted magnet's request as the server answers it. Everything is
// untrusted and checked strictly: a state outside the fixed list, a size that
// is not a whole non-negative number, an id of the wrong shape, or a play
// link whose path is not exactly `/play/m1.<token>/<file name>` makes the
// whole answer an error. A play link is kept only as that path, rebuilt on
// the signed-in server: the server writes its links on its public API
// address, which may be another host than the one this app talks to, and
// whatever host it names is never opened. Names are display only, so they
// are cut rather than refused.
namespace
{
    using winrt::Windows::Data::Json::IJsonValue;
    using winrt::Windows::Data::Json::JsonObject;
    using winrt::Windows::Data::Json::JsonValueType;

    namespace Dto = ::HaloDesktop::Api::Dto;

    constexpr std::size_t MaximumIdLength = 64;
    constexpr std::size_t MaximumFileIdLength = 32;
    constexpr std::size_t MaximumReasonLength = 40;
    constexpr std::size_t MaximumNameLength = 300;
    constexpr std::size_t MaximumTitleNameLength = 200;
    constexpr std::size_t MaximumPlayUrlLength = 4096;
    // The server lists at most 200 video files.
    constexpr std::size_t MaximumFiles = 200;
    constexpr std::int32_t MaximumSeason = 1000;
    constexpr std::int32_t MaximumEpisode = 10000;
    constexpr double MaximumSafeInteger = 9007199254740991.0;

    [[noreturn]] void Refuse(char const* why)
    {
        throw std::invalid_argument{ why };
    }

    bool IsAsciiDigit(wchar_t character) noexcept { return character >= L'0' && character <= L'9'; }
    bool IsAsciiLetter(wchar_t character) noexcept
    {
        return (character >= L'a' && character <= L'z') || (character >= L'A' && character <= L'Z');
    }

    wchar_t AsciiLower(wchar_t character) noexcept
    {
        return character >= L'A' && character <= L'Z' ? static_cast<wchar_t>(character - L'A' + L'a') : character;
    }

    std::wstring AsciiLower(std::wstring_view text)
    {
        std::wstring result{ text };
        std::transform(result.begin(), result.end(), result.begin(), [](wchar_t character) { return AsciiLower(character); });
        return result;
    }

    // A field that is present must have the expected type; absent is fine.
    std::optional<IJsonValue> Field(JsonObject const& object, wchar_t const* key, JsonValueType type, char const* why)
    {
        if (!object.HasKey(key)) return std::nullopt;
        auto const value = object.GetNamedValue(key);
        if (value.ValueType() == JsonValueType::Null) return std::nullopt;
        if (value.ValueType() != type) Refuse(why);
        return value;
    }

    IJsonValue Required(JsonObject const& object, wchar_t const* key, JsonValueType type, char const* why)
    {
        auto value = Field(object, key, type, why);
        if (!value) Refuse(why);
        return *value;
    }

    // Trimmed, control characters as spaces, cut at `maximum` (never on half
    // of a surrogate pair).
    std::wstring DisplayText(std::wstring_view raw, std::size_t maximum)
    {
        auto const isSpace = [](wchar_t character) { return std::iswspace(character) != 0; };
        while (!raw.empty() && isSpace(raw.front())) raw.remove_prefix(1);
        while (!raw.empty() && isSpace(raw.back())) raw.remove_suffix(1);
        std::wstring text{ raw.substr(0, maximum) };
        if (!text.empty() && text.back() >= 0xD800 && text.back() <= 0xDBFF) text.pop_back();
        std::replace_if(text.begin(), text.end(), [](wchar_t character) { return std::iswcntrl(character) != 0; }, L' ');
        while (!text.empty() && isSpace(text.back())) text.pop_back();
        return text;
    }

    std::uint64_t WholeSize(JsonObject const& object, wchar_t const* key, char const* why)
    {
        auto const number = Required(object, key, JsonValueType::Number, why).GetNumber();
        if (!std::isfinite(number) || number < 0.0 || number > MaximumSafeInteger || std::floor(number) != number) Refuse(why);
        return static_cast<std::uint64_t>(number);
    }

    std::optional<std::int32_t> SmallCount(JsonObject const& object, wchar_t const* key, std::int32_t maximum, char const* why)
    {
        auto const value = Field(object, key, JsonValueType::Number, why);
        if (!value) return std::nullopt;
        auto const number = value->GetNumber();
        if (!std::isfinite(number) || number < 0.0 || number > static_cast<double>(maximum) || std::floor(number) != number) Refuse(why);
        return static_cast<std::int32_t>(number);
    }

    // `[A-Za-z0-9_-]` only, 1 to `maximum` characters.
    bool IsPlainId(std::wstring_view text, std::size_t maximum) noexcept
    {
        return !text.empty() && text.size() <= maximum && std::all_of(text.begin(), text.end(), [](wchar_t character)
        {
            return IsAsciiLetter(character) || IsAsciiDigit(character) || character == L'_' || character == L'-';
        });
    }

    Dto::MagnetState StateOf(std::wstring_view text)
    {
        if (text == L"ready") return Dto::MagnetState::Ready;
        if (text == L"not_cached") return Dto::MagnetState::NotCached;
        if (text == L"downloading") return Dto::MagnetState::Downloading;
        if (text == L"no_video") return Dto::MagnetState::NoVideo;
        if (text == L"too_large") return Dto::MagnetState::TooLarge;
        if (text == L"failed") return Dto::MagnetState::Failed;
        Refuse("The magnet answer has an unknown state.");
    }

    std::optional<winrt::hstring> Reason(JsonObject const& object)
    {
        constexpr char const* why = "The magnet answer has a reason of the wrong shape.";
        auto const value = Field(object, L"reason", JsonValueType::String, why);
        if (!value) return std::nullopt;
        auto const text = value->GetString();
        std::wstring_view const view{ text };
        auto const plain = !view.empty() && view.size() <= MaximumReasonLength
            && std::all_of(view.begin(), view.end(), [](wchar_t character) { return (character >= L'a' && character <= L'z') || character == L'_'; });
        if (!plain) Refuse(why);
        return text;
    }

    Dto::MagnetFile ParseFile(IJsonValue const& item, std::wstring_view serverUrl)
    {
        constexpr char const* why = "The magnet answer lists a file of the wrong shape.";
        if (item.ValueType() != JsonValueType::Object) Refuse(why);
        auto const object = item.GetObject();
        auto const id = Required(object, L"id", JsonValueType::String, why).GetString();
        if (!IsPlainId(id, MaximumFileIdLength)) Refuse(why);
        auto name = DisplayText(Required(object, L"name", JsonValueType::String, why).GetString(), MaximumNameLength);
        if (name.empty()) Refuse(why);
        auto const playUrl = ::HaloDesktop::Api::Mappers::MagnetPlayLinkOn(
            std::wstring_view{ Required(object, L"playUrl", JsonValueType::String, why).GetString() },
            serverUrl);
        if (!playUrl)
        {
            Refuse("The magnet answer has a play link of the wrong shape.");
        }
        return Dto::MagnetFile{
            .Id = id,
            .Name = winrt::hstring{ name },
            .SizeBytes = WholeSize(object, L"sizeBytes", why),
            .Season = SmallCount(object, L"season", MaximumSeason, why),
            .Episode = SmallCount(object, L"episode", MaximumEpisode, why),
            .PlayUrl = winrt::hstring{ *playUrl },
        };
    }

    std::optional<Dto::MagnetIdentity> ParseIdentity(JsonObject const& root)
    {
        constexpr char const* why = "The magnet answer names a title of the wrong shape.";
        auto const value = Field(root, L"identified", JsonValueType::Object, why);
        if (!value) return std::nullopt;
        auto const object = value->GetObject();
        auto const type = Required(object, L"type", JsonValueType::String, why).GetString();
        if (type != L"movie" && type != L"series") Refuse(why);
        auto const id = Required(object, L"id", JsonValueType::String, why).GetString();
        std::wstring_view const idView{ id };
        if (idView.size() < 3 || idView.size() > 12 || !idView.starts_with(L"tt")
            || !std::all_of(idView.begin() + 2, idView.end(), IsAsciiDigit))
        {
            Refuse(why);
        }
        auto const name = DisplayText(Required(object, L"name", JsonValueType::String, why).GetString(), MaximumTitleNameLength);
        if (name.empty()) Refuse(why);
        std::optional<winrt::hstring> year;
        if (auto const yearValue = Field(object, L"year", JsonValueType::String, why))
        {
            auto const text = yearValue->GetString();
            std::wstring_view const yearView{ text };
            if (!yearView.empty())
            {
                if (yearView.size() != 4 || !std::all_of(yearView.begin(), yearView.end(), IsAsciiDigit)) Refuse(why);
                year = text;
            }
        }
        // Artwork only from TMDB's own image host; any other poster is left
        // out (the card falls back on its placeholder) rather than refused.
        std::optional<winrt::hstring> poster;
        if (auto const posterValue = Field(object, L"poster", JsonValueType::String, why))
        {
            auto const text = posterValue->GetString();
            if (::HaloDesktop::Api::Mappers::IsTmdbImageUrl(text)) poster = text;
        }
        return Dto::MagnetIdentity{
            .Type = type,
            .Id = id,
            .Name = winrt::hstring{ name },
            .Year = std::move(year),
            .Poster = std::move(poster),
        };
    }

    struct Origin final
    {
        std::wstring Scheme;
        std::wstring Host;
        std::wstring Port;
        // What follows the authority: empty, or from its `/`, `?` or `#` on.
        std::wstring_view Rest;
    };

    // scheme://host[:port] of an absolute http(s) link, or nothing. No user
    // name or password, no white space or control characters, a host of
    // letters, digits, dots and hyphens (or a bracketed IPv6 literal).
    std::optional<Origin> OriginOf(std::wstring_view url)
    {
        auto const schemeEnd = url.find(L"://");
        if (schemeEnd == std::wstring_view::npos || schemeEnd == 0) return std::nullopt;
        Origin origin;
        origin.Scheme = AsciiLower(url.substr(0, schemeEnd));
        if (origin.Scheme != L"http" && origin.Scheme != L"https") return std::nullopt;
        auto const afterScheme = url.substr(schemeEnd + 3);
        auto const authorityEnd = afterScheme.find_first_of(L"/?#");
        auto const authority = afterScheme.substr(0, authorityEnd);
        origin.Rest = authorityEnd == std::wstring_view::npos ? std::wstring_view{} : afterScheme.substr(authorityEnd);
        if (authority.empty()) return std::nullopt;

        std::wstring_view host = authority;
        std::wstring_view port;
        bool portGiven{};
        if (authority.front() == L'[')
        {
            auto const close = authority.find(L']');
            if (close == std::wstring_view::npos) return std::nullopt;
            host = authority.substr(0, close + 1);
            auto const after = authority.substr(close + 1);
            if (!after.empty())
            {
                if (after.front() != L':') return std::nullopt;
                port = after.substr(1);
                portGiven = true;
            }
            auto const inside = host.substr(1, host.size() - 2);
            if (inside.empty() || !std::all_of(inside.begin(), inside.end(), [](wchar_t character)
                {
                    return IsAsciiDigit(character) || (character >= L'a' && character <= L'f')
                        || (character >= L'A' && character <= L'F') || character == L':' || character == L'.';
                }))
            {
                return std::nullopt;
            }
        }
        else
        {
            auto const colon = authority.rfind(L':');
            if (colon != std::wstring_view::npos)
            {
                host = authority.substr(0, colon);
                port = authority.substr(colon + 1);
                portGiven = true;
            }
            if (host.empty() || !std::all_of(host.begin(), host.end(), [](wchar_t character)
                {
                    return IsAsciiLetter(character) || IsAsciiDigit(character) || character == L'.' || character == L'-';
                }))
            {
                return std::nullopt;
            }
        }
        if (portGiven && port.empty()) return std::nullopt;
        if (!port.empty())
        {
            if (port.size() > 5 || !std::all_of(port.begin(), port.end(), IsAsciiDigit)) return std::nullopt;
            // Without leading zeros, so 443 and 0443 cannot read as different.
            while (port.size() > 1 && port.front() == L'0') port.remove_prefix(1);
        }
        origin.Host = AsciiLower(host);
        origin.Port = port.empty() ? (origin.Scheme == L"https" ? L"443" : L"80") : std::wstring{ port };
        return origin;
    }

    // A path of printable ASCII only, as the server writes its links (the
    // file name is percent-encoded). No fragment, no backslash.
    bool IsPlainPath(std::wstring_view rest) noexcept
    {
        if (rest.empty() || rest.front() != L'/') return false;
        return std::all_of(rest.begin(), rest.end(), [](wchar_t character)
        {
            if (character <= L' ' || character >= 0x7F) return false;
            return character != L'\\' && character != L'#' && character != L'"' && character != L'<'
                && character != L'>' && character != L'`' && character != L'{' && character != L'}'
                && character != L'|' && character != L'^';
        });
    }

    constexpr std::wstring_view PlaySegment = L"/play/";
    constexpr std::wstring_view MagnetTokenVersion = L"m1.";
    constexpr std::size_t MaximumTokenLength = 2048;

    bool IsBase64UrlPart(std::wstring_view part) noexcept
    {
        return !part.empty() && std::all_of(part.begin(), part.end(), [](wchar_t character)
        {
            return IsAsciiLetter(character) || IsAsciiDigit(character) || character == L'_' || character == L'-';
        });
    }

    // `m1.<payload>.<signature>`, both parts base64url, as the server signs a
    // magnet's play token. A stream addon's token (another version) is not one.
    bool IsMagnetToken(std::wstring_view token) noexcept
    {
        if (token.size() > MaximumTokenLength || !token.starts_with(MagnetTokenVersion)) return false;
        auto const rest = token.substr(MagnetTokenVersion.size());
        auto const dot = rest.find(L'.');
        if (dot == std::wstring_view::npos) return false;
        return IsBase64UrlPart(rest.substr(0, dot)) && IsBase64UrlPart(rest.substr(dot + 1));
    }

    bool ContainsInsensitive(std::wstring_view text, std::wstring_view needle)
    {
        return AsciiLower(text).find(needle) != std::wstring::npos;
    }

    // The percent-encoded file name: never a dot segment, and never an
    // encoded dot, slash or backslash a client might decode into one.
    bool IsPlayFileName(std::wstring_view name)
    {
        if (name.empty() || name == L"." || name == L"..") return false;
        return !ContainsInsensitive(name, L"%2e") && !ContainsInsensitive(name, L"%2f") && !ContainsInsensitive(name, L"%5c");
    }
}

namespace HaloDesktop::Api::Mappers
{
    std::optional<std::wstring> MagnetPlayLinkOn(std::wstring_view url, std::wstring_view serverUrl)
    {
        if (url.empty() || url.size() > MaximumPlayUrlLength) return std::nullopt;
        // The link must be a well-formed http(s) link, but its scheme, host
        // and port are only checked, never used.
        auto const link = OriginOf(url);
        if (!link || !IsPlainPath(link->Rest) || link->Rest.find(L'?') != std::wstring_view::npos) return std::nullopt;
        auto const at = link->Rest.rfind(PlaySegment);
        if (at == std::wstring_view::npos) return std::nullopt;
        auto const tail = link->Rest.substr(at);

        // Exactly `/play/<token>/<file name>`.
        auto const segments = tail.substr(PlaySegment.size());
        auto const slash = segments.find(L'/');
        if (slash == std::wstring_view::npos) return std::nullopt;
        auto const token = segments.substr(0, slash);
        auto const name = segments.substr(slash + 1);
        if (!IsMagnetToken(token) || name.find(L'/') != std::wstring_view::npos || !IsPlayFileName(name)) return std::nullopt;

        // On the app's own server, joined as every other call joins its path
        // (ApiClient::Endpoint), so a self-hosted server works the same way.
        auto const server = OriginOf(serverUrl);
        if (!server || server->Rest.find_first_of(L"?#") != std::wstring_view::npos) return std::nullopt;
        std::wstring base{ serverUrl };
        while (!base.empty() && base.back() == L'/') base.pop_back();
        return base + std::wstring{ tail };
    }

    Dto::MagnetRequest ParseMagnet(IJsonValue const& value, std::wstring_view serverUrl)
    {
        if (!value || value.ValueType() != JsonValueType::Object) Refuse("The magnet answer must be an object.");
        auto const root = value.GetObject();
        constexpr char const* why = "The magnet answer has a field of the wrong shape.";

        auto const id = Required(root, L"id", JsonValueType::String, why).GetString();
        std::wstring_view const idView{ id };
        if (!idView.starts_with(L"mg_") || !IsPlainId(idView.substr(3), MaximumIdLength)) Refuse(why);

        Dto::MagnetRequest request{
            .Id = id,
            .State = StateOf(std::wstring_view{ Required(root, L"state", JsonValueType::String, why).GetString() }),
            .Reason = Reason(root),
            .SizeBytes = WholeSize(root, L"sizeBytes", why),
        };
        if (auto const name = Field(root, L"name", JsonValueType::String, why))
        {
            request.Name = winrt::hstring{ DisplayText(name->GetString(), MaximumNameLength) };
        }
        if (auto const progress = Field(root, L"progress", JsonValueType::Number, why))
        {
            auto const number = progress->GetNumber();
            if (!std::isfinite(number) || number < 0.0 || number > 1.0) Refuse(why);
            request.Progress = number;
        }
        request.Identified = ParseIdentity(root);

        // Files belong to a ready request only, and a ready one has some.
        if (request.State != Dto::MagnetState::Ready) return request;
        auto const files = Required(root, L"files", JsonValueType::Array, "A ready magnet answer has no files.").GetArray();
        if (files.Size() == 0 || files.Size() > MaximumFiles) Refuse("A ready magnet answer has no files, or too many.");
        std::unordered_set<std::wstring> ids;
        for (auto const& item : files)
        {
            auto file = ParseFile(item, serverUrl);
            if (!ids.insert(std::wstring{ file.Id }).second) Refuse("The magnet answer lists one file twice.");
            request.Files.push_back(std::move(file));
        }
        return request;
    }
}
