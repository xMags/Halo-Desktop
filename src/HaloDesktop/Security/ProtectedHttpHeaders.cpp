#include "Security/ProtectedHttpHeaders.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace
{
    std::wstring Lowercase(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character)
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

    // Names the player or the connection owns: the hop-by-hop and conditional
    // names this app has always refused, which include all of the reference's
    // list but one.
    constexpr std::array<std::wstring_view, 13> DeniedNames{
        L"connection", L"content-length", L"host", L"if-range", L"keep-alive",
        L"proxy-authenticate", L"proxy-authorization", L"proxy-connection", L"range",
        L"te", L"trailer", L"transfer-encoding", L"upgrade",
    };
    // The reference's remaining name. Only headers on their way in from an addon
    // drop it: a download request stored with it before keeps validating, so the
    // job still restores.
    constexpr std::wstring_view DeniedIncomingName = L"accept-encoding";

    constexpr std::size_t MaximumHeaderCount = 64;
    constexpr std::size_t MaximumNameLength = 128;
    // What an addon header may carry on the way in. Records already stored
    // (download requests) are checked against the older, wider bound below so a
    // job saved before this limit still restores.
    constexpr std::size_t MaximumUsableValueLength = 4096;
    constexpr std::size_t MaximumStoredValueLength = 8192;

    bool IsSafeHeader(std::wstring const& name, std::wstring const& value, std::size_t maximumValueLength)
    {
        auto const lower = Lowercase(name);
        return name.size() <= MaximumNameLength && value.size() <= maximumValueLength && IsHeaderToken(name)
            && value.find_first_of(L"\r\n") == std::wstring::npos
            && value.find(L'\0') == std::wstring::npos
            && std::find(DeniedNames.begin(), DeniedNames.end(), lower) == DeniedNames.end();
    }

    // A value with a lone surrogate cannot be encoded for mpv or WinHTTP; it is
    // dropped with the other unusable headers rather than failing the request.
    bool IsWellFormedUtf16(std::wstring const& value) noexcept
    {
        for (std::size_t index = 0; index < value.size(); ++index)
        {
            auto const unit = value[index];
            if (unit >= 0xD800 && unit <= 0xDBFF)
            {
                if (index + 1 >= value.size() || value[index + 1] < 0xDC00 || value[index + 1] > 0xDFFF) return false;
                ++index;
            }
            else if (unit >= 0xDC00 && unit <= 0xDFFF)
            {
                return false;
            }
        }
        return true;
    }

    void ValidateHeader(std::wstring const& name, std::wstring const& value)
    {
        if (!IsSafeHeader(name, value, MaximumStoredValueLength))
        {
            throw std::invalid_argument{ "The protected source headers are not safe." };
        }
    }

    template<typename Headers, typename ReadHeader>
    void ValidateHeaders(Headers const& headers, ReadHeader readHeader)
    {
        if (headers.size() > MaximumHeaderCount)
        {
            throw std::invalid_argument{ "The source supplied too many request headers." };
        }
        for (auto const& header : headers)
        {
            auto const [name, value] = readHeader(header);
            ValidateHeader(name, value);
        }
    }
}

namespace HaloDesktop::Security
{
    void ValidateProtectedHttpHeaders(ProtectedHttpHeaders const& headers)
    {
        ValidateHeaders(headers, [](ProtectedHttpHeader const& header)
        {
            return std::pair<std::wstring const&, std::wstring const&>{ header.Name, header.Value };
        });
    }

    void ValidateProtectedHttpHeaders(
        std::map<std::wstring, std::wstring, std::less<>> const& headers)
    {
        ValidateHeaders(headers, [](auto const& header)
        {
            return std::pair<std::wstring const&, std::wstring const&>{ header.first, header.second };
        });
    }

    bool IsUsableProtectedHttpHeader(std::wstring const& name, std::wstring const& value)
    {
        return IsSafeHeader(name, value, MaximumUsableValueLength) && Lowercase(name) != DeniedIncomingName
            && IsWellFormedUtf16(value);
    }

    ProtectedHttpHeaders UsableProtectedHttpHeaders(ProtectedHttpHeaders const& headers)
    {
        ProtectedHttpHeaders usable;
        auto const considered = (std::min)(headers.size(), MaximumHeaderCount);
        usable.reserve(considered);
        for (std::size_t index = 0; index < considered; ++index)
        {
            auto const& header = headers[index];
            if (IsUsableProtectedHttpHeader(header.Name, header.Value))
            {
                usable.push_back(header);
            }
        }
        return usable;
    }
}
