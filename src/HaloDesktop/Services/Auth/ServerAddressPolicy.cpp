#include "Services/Auth/ServerAddressPolicy.h"

#include <cstdint>
#include <cwctype>
#include <vector>

namespace
{
    using HaloDesktop::Services::Auth::ServerAddressResult;

    ServerAddressResult Refuse(std::wstring_view error)
    {
        return ServerAddressResult{ .Address = std::nullopt, .Error = std::wstring{ error } };
    }

    bool IsAsciiLetter(wchar_t character) noexcept
    {
        return (character >= L'a' && character <= L'z') || (character >= L'A' && character <= L'Z');
    }

    bool IsAsciiDigit(wchar_t character) noexcept
    {
        return character >= L'0' && character <= L'9';
    }

    wchar_t AsciiLower(wchar_t character) noexcept
    {
        return character >= L'A' && character <= L'Z'
            ? static_cast<wchar_t>(character - L'A' + L'a')
            : character;
    }

    std::wstring_view Trimmed(std::wstring_view value) noexcept
    {
        while (!value.empty() && std::iswspace(value.front()) != 0)
        {
            value.remove_prefix(1);
        }
        while (!value.empty() && std::iswspace(value.back()) != 0)
        {
            value.remove_suffix(1);
        }
        return value;
    }

    // `^[a-z][a-z0-9+.-]*://`, case-insensitive, as the reference tests it.
    bool StartsWithScheme(std::wstring_view value) noexcept
    {
        if (value.empty() || !IsAsciiLetter(value.front()))
        {
            return false;
        }
        std::size_t index = 1;
        while (index < value.size())
        {
            auto const character = value[index];
            if (IsAsciiLetter(character) || IsAsciiDigit(character)
                || character == L'+' || character == L'.' || character == L'-')
            {
                ++index;
                continue;
            }
            break;
        }
        return value.substr(index).starts_with(L"://");
    }

    bool IsHostCharacter(wchar_t character) noexcept
    {
        return IsAsciiLetter(character) || IsAsciiDigit(character)
            || character == L'-' || character == L'.' || character == L'_';
    }

    bool IsIpv6Character(wchar_t character) noexcept
    {
        return IsAsciiDigit(character) || (character >= L'a' && character <= L'f')
            || (character >= L'A' && character <= L'F') || character == L':' || character == L'.';
    }

    // Resolves `.` and `..` segments the way a URL parser does, so the address
    // that is kept is the one requests are sent to.
    std::wstring NormalizedPath(std::wstring_view path)
    {
        std::vector<std::wstring_view> segments;
        std::size_t start = 1;
        while (start <= path.size())
        {
            auto end = path.find(L'/', start);
            if (end == std::wstring_view::npos)
            {
                end = path.size();
            }
            auto const segment = path.substr(start, end - start);
            if (segment == L"..")
            {
                if (!segments.empty())
                {
                    segments.pop_back();
                }
            }
            else if (segment != L".")
            {
                segments.push_back(segment);
            }
            start = end + 1;
        }
        std::wstring result;
        for (auto const segment : segments)
        {
            result.push_back(L'/');
            result.append(segment);
        }
        return result;
    }
}

namespace HaloDesktop::Services::Auth
{
    bool IsLoopbackHost(std::wstring_view host) noexcept
    {
        return host == L"127.0.0.1" || host == L"localhost" || host == L"[::1]";
    }

    ServerAddressResult ParseServerAddress(std::wstring_view raw)
    {
        auto const trimmed = Trimmed(raw);
        if (trimmed.empty())
        {
            return Refuse(ServerAddressEmptyError);
        }
        if (raw.size() > MaximumServerAddressLength)
        {
            return Refuse(ServerAddressInvalidError);
        }
        for (auto const character : trimmed)
        {
            if (std::iswcntrl(character) != 0 || std::iswspace(character) != 0 || character == L'\\')
            {
                return Refuse(ServerAddressInvalidError);
            }
        }

        std::wstring value{ trimmed };
        if (!StartsWithScheme(value))
        {
            value.insert(0, L"https://");
        }

        auto const schemeEnd = value.find(L"://");
        std::wstring scheme;
        for (auto const character : std::wstring_view{ value }.substr(0, schemeEnd))
        {
            scheme.push_back(AsciiLower(character));
        }
        auto const rest = std::wstring_view{ value }.substr(schemeEnd + 3);
        auto const authorityEnd = rest.find_first_of(L"/?#");
        auto const authority = rest.substr(0, authorityEnd);
        auto const tail = authorityEnd == std::wstring_view::npos ? std::wstring_view{} : rest.substr(authorityEnd);

        // Credentials are reported after the scheme, as the reference orders
        // its checks; an empty user info (`https://@host`) is no credential.
        auto hostAndPort = authority;
        auto hasCredentials = false;
        if (auto const at = authority.rfind(L'@'); at != std::wstring_view::npos)
        {
            for (auto const character : authority.substr(0, at))
            {
                if (character != L':')
                {
                    hasCredentials = true;
                    break;
                }
            }
            hostAndPort = authority.substr(at + 1);
        }

        std::wstring host;
        std::wstring_view port;
        if (!hostAndPort.empty() && hostAndPort.front() == L'[')
        {
            auto const close = hostAndPort.find(L']');
            if (close == std::wstring_view::npos || close == 1)
            {
                return Refuse(ServerAddressInvalidError);
            }
            for (auto const character : hostAndPort.substr(1, close - 1))
            {
                if (!IsIpv6Character(character))
                {
                    return Refuse(ServerAddressInvalidError);
                }
            }
            host.push_back(L'[');
            for (auto const character : hostAndPort.substr(1, close - 1))
            {
                host.push_back(AsciiLower(character));
            }
            host.push_back(L']');
            auto const after = hostAndPort.substr(close + 1);
            if (!after.empty())
            {
                if (after.front() != L':')
                {
                    return Refuse(ServerAddressInvalidError);
                }
                port = after.substr(1);
            }
        }
        else
        {
            auto const colon = hostAndPort.find(L':');
            auto const name = hostAndPort.substr(0, colon);
            if (colon != std::wstring_view::npos)
            {
                port = hostAndPort.substr(colon + 1);
            }
            for (auto const character : name)
            {
                if (!IsHostCharacter(character))
                {
                    return Refuse(ServerAddressInvalidError);
                }
                host.push_back(AsciiLower(character));
            }
        }
        if (host.empty() || host.find_first_not_of(L'.') == std::wstring::npos)
        {
            return Refuse(ServerAddressInvalidError);
        }

        std::optional<std::uint32_t> portNumber;
        if (!port.empty())
        {
            std::uint32_t number{};
            for (auto const character : port)
            {
                if (!IsAsciiDigit(character))
                {
                    return Refuse(ServerAddressInvalidError);
                }
                number = number * 10u + static_cast<std::uint32_t>(character - L'0');
                if (number > 65535u)
                {
                    return Refuse(ServerAddressInvalidError);
                }
            }
            portNumber = number;
        }

        auto const loopback = IsLoopbackHost(host);
        if (scheme != L"https" && !(scheme == L"http" && loopback))
        {
            return Refuse(ServerAddressInsecureError);
        }
        if (hasCredentials || tail.find_first_of(L"?#") != std::wstring_view::npos)
        {
            return Refuse(ServerAddressExtraError);
        }

        std::wstring address = scheme + L"://" + host;
        auto const defaultPort = scheme == L"https" ? 443u : 80u;
        if (portNumber && *portNumber != defaultPort)
        {
            address.push_back(L':');
            address.append(std::to_wstring(*portNumber));
        }
        address.append(NormalizedPath(tail));
        while (!address.empty() && address.back() == L'/')
        {
            address.pop_back();
        }
        return ServerAddressResult{ .Address = std::move(address), .Error = {} };
    }

    SessionServerCheck CheckSessionServer(
        std::optional<std::wstring_view> recorded,
        std::wstring_view builtIn)
    {
        if (!recorded || recorded->empty())
        {
            return SessionServerCheck::Unrecorded;
        }
        auto withoutSlash = [](std::wstring_view url)
        {
            while (!url.empty() && url.back() == L'/')
            {
                url.remove_suffix(1);
            }
            return url;
        };
        return withoutSlash(*recorded) == withoutSlash(builtIn)
            ? SessionServerCheck::Matches
            : SessionServerCheck::Different;
    }
}
