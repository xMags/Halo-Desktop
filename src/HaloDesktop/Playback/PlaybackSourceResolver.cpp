#include "pch.h"
#include "Playback/PlaybackSourceResolver.h"

#include "Security/ProtectedRedirect.h"

#include <optional>
#include <string>
#include <utility>
#include <wil/resource.h>
#include <winhttp.h>

namespace
{
    struct HttpCloser final
    {
        void operator()(void* value) const noexcept
        {
            if (value)
            {
                WinHttpCloseHandle(value);
            }
        }
    };
    using UniqueHttp = std::unique_ptr<void, HttpCloser>;

    struct ProbeResult final
    {
        std::uint32_t Status{};
        std::optional<std::wstring> Location;
    };

    bool IsHttpUrl(std::wstring const& url) noexcept
    {
        return url.starts_with(L"http://") || url.starts_with(L"https://");
    }

    std::optional<std::wstring> QueryLocation(HINTERNET request)
    {
        DWORD bytes{};
        WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_LOCATION,
            WINHTTP_HEADER_NAME_BY_INDEX,
            WINHTTP_NO_OUTPUT_BUFFER,
            &bytes,
            WINHTTP_NO_HEADER_INDEX);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER
            || bytes < sizeof(wchar_t)
            || bytes > HaloDesktop::Security::MaximumLocationLength * sizeof(wchar_t))
        {
            return std::nullopt;
        }
        std::wstring value(bytes / sizeof(wchar_t), L'\0');
        if (!WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_LOCATION,
            WINHTTP_HEADER_NAME_BY_INDEX,
            value.data(),
            &bytes,
            WINHTTP_NO_HEADER_INDEX))
        {
            return std::nullopt;
        }
        value.resize(bytes / sizeof(wchar_t));
        while (!value.empty() && value.back() == L'\0')
        {
            value.pop_back();
        }
        if (value.empty())
        {
            return std::nullopt;
        }
        return value;
    }

    // One hop. Asks for a single byte so a server that answers cannot start
    // streaming the file at us, and never reads the body.
    std::optional<ProbeResult> ProbeOnce(
        std::wstring const& url,
        std::vector<HaloDesktop::Playback::PlaybackHeader> const& headers)
    {
        URL_COMPONENTS parts{ .dwStructSize = sizeof(URL_COMPONENTS) };
        parts.dwSchemeLength = static_cast<DWORD>(-1);
        parts.dwHostNameLength = static_cast<DWORD>(-1);
        parts.dwUrlPathLength = static_cast<DWORD>(-1);
        parts.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &parts))
        {
            return std::nullopt;
        }
        std::wstring const host(parts.lpszHostName, parts.dwHostNameLength);
        std::wstring target;
        if (parts.dwUrlPathLength > 0)
        {
            target.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
        }
        if (parts.dwExtraInfoLength > 0)
        {
            target.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        }
        if (target.empty())
        {
            target = L"/";
        }

        UniqueHttp session{ WinHttpOpen(
            L"Halo Desktop/1.0",
            WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0) };
        if (!session)
        {
            return std::nullopt;
        }
        // Kept short. This runs before the first frame, so a source that is slow
        // to answer should fall back to the old path rather than stall playback.
        if (!WinHttpSetTimeouts(session.get(), 5000, 5000, 5000, 10000))
        {
            return std::nullopt;
        }
        UniqueHttp connection{ WinHttpConnect(session.get(), host.c_str(), parts.nPort, 0) };
        if (!connection)
        {
            return std::nullopt;
        }
        auto const flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
        UniqueHttp request{ WinHttpOpenRequest(
            connection.get(),
            L"GET",
            target.c_str(),
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            flags) };
        if (!request)
        {
            return std::nullopt;
        }
        DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        if (!WinHttpSetOption(
            request.get(),
            WINHTTP_OPTION_REDIRECT_POLICY,
            &redirectPolicy,
            sizeof(redirectPolicy)))
        {
            return std::nullopt;
        }
        for (auto const& header : headers)
        {
            auto const line = header.Name + L": " + header.Value;
            if (!WinHttpAddRequestHeaders(
                request.get(),
                line.c_str(),
                static_cast<DWORD>(line.size()),
                WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE))
            {
                return std::nullopt;
            }
        }
        constexpr std::wstring_view range = L"Range: bytes=0-0";
        if (!WinHttpAddRequestHeaders(
            request.get(),
            range.data(),
            static_cast<DWORD>(range.size()),
            WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE))
        {
            return std::nullopt;
        }
        if (!WinHttpSendRequest(
            request.get(),
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0)
            || !WinHttpReceiveResponse(request.get(), nullptr))
        {
            return std::nullopt;
        }
        DWORD status{};
        DWORD statusBytes = sizeof(status);
        if (!WinHttpQueryHeaders(
            request.get(),
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status,
            &statusBytes,
            WINHTTP_NO_HEADER_INDEX))
        {
            return std::nullopt;
        }
        ProbeResult result{ .Status = status };
        if (HaloDesktop::Security::IsRedirectStatus(status))
        {
            result.Location = QueryLocation(request.get());
        }
        return result;
    }
}

namespace HaloDesktop::Playback
{
    PlaybackSource ResolvePlaybackSource(PlaybackSource source) noexcept
    {
        try
        {
            if (source.Headers.empty() || !IsHttpUrl(source.Location))
            {
                return source;
            }

            auto currentUrl = source.Location;
            bool forwardHeaders = true;
            for (int hop = 0;; ++hop)
            {
                auto const probe = ProbeOnce(currentUrl, forwardHeaders ? source.Headers : decltype(source.Headers){});
                if (!probe)
                {
                    return source;
                }
                if (!Security::IsRedirectStatus(probe->Status))
                {
                    if (probe->Status < 200 || probe->Status > 299)
                    {
                        // The source did not answer the way libmpv will be asked
                        // to open it. Nothing was learnt about its origin, so
                        // nothing is changed.
                        return source;
                    }
                    PlaybackSource resolved{ .Location = std::move(currentUrl) };
                    if (forwardHeaders)
                    {
                        resolved.Headers = std::move(source.Headers);
                    }
                    return resolved;
                }
                if (!probe->Location)
                {
                    return source;
                }
                auto const target = Security::NextRedirectTarget(currentUrl, *probe->Location, hop);
                if (!target)
                {
                    return source;
                }
                if (!target->SameOrigin)
                {
                    forwardHeaders = false;
                }
                currentUrl = target->Url;
            }
        }
        catch (...)
        {
            return source;
        }
    }
}
