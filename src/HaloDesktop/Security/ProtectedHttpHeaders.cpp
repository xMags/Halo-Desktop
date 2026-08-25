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

    void ValidateHeader(std::wstring const& name, std::wstring const& value)
    {
        constexpr std::array<std::wstring_view, 13> denied{
            L"connection", L"content-length", L"host", L"if-range", L"keep-alive",
            L"proxy-authenticate", L"proxy-authorization", L"proxy-connection", L"range",
            L"te", L"trailer", L"transfer-encoding", L"upgrade",
        };

        auto const lower = Lowercase(name);
        if (name.size() > 128 || value.size() > 8192 || !IsHeaderToken(name)
            || value.find_first_of(L"\r\n") != std::wstring::npos
            || value.find(L'\0') != std::wstring::npos
            || std::find(denied.begin(), denied.end(), lower) != denied.end())
        {
            throw std::invalid_argument{ "The protected source headers are not safe." };
        }
    }

    template<typename Headers, typename ReadHeader>
    void ValidateHeaders(Headers const& headers, ReadHeader readHeader)
    {
        if (headers.size() > 64)
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
}
