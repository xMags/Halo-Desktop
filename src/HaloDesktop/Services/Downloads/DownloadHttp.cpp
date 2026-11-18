#include "pch.h"
#include "Services/Downloads/DownloadHttp.h"

#include "Security/ProtectedRedirect.h"

#include <condition_variable>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>

namespace
{
    namespace Http = HaloDesktop::Services::Downloads::Http;

    [[noreturn]] void ThrowSystem(DWORD error, char const* message)
    {
        throw std::system_error{ static_cast<int>(error), std::system_category(), message };
    }

    std::optional<std::wstring> QueryHeader(HINTERNET request, wchar_t const* headerName)
    {
        DWORD bytes{};
        WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_CUSTOM,
            headerName,
            WINHTTP_NO_OUTPUT_BUFFER,
            &bytes,
            WINHTTP_NO_HEADER_INDEX);
        auto const error = GetLastError();
        if (error == ERROR_WINHTTP_HEADER_NOT_FOUND)
        {
            return std::nullopt;
        }
        if (error != ERROR_INSUFFICIENT_BUFFER || bytes < sizeof(wchar_t))
        {
            ThrowSystem(error, "A download response header could not be inspected");
        }
        if (bytes > 64u * 1024u)
        {
            throw std::length_error{ "A download response header is oversized." };
        }
        std::wstring value(bytes / sizeof(wchar_t), L'\0');
        if (!WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_CUSTOM,
            headerName,
            value.data(),
            &bytes,
            WINHTTP_NO_HEADER_INDEX))
        {
            ThrowSystem(GetLastError(), "A download response header could not be read");
        }
        value.resize(bytes / sizeof(wchar_t));
        while (!value.empty() && value.back() == L'\0')
        {
            value.pop_back();
        }
        return value.empty() ? std::nullopt : std::optional<std::wstring>{ std::move(value) };
    }

    void AddHeader(HINTERNET request, std::wstring const& line, char const* failure)
    {
        if (!WinHttpAddRequestHeaders(
            request,
            line.c_str(),
            static_cast<DWORD>(line.size()),
            WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE))
        {
            ThrowSystem(GetLastError(), failure);
        }
    }

    Http::Response OpenGetOnce(
        std::wstring const& url,
        Http::HeaderMap const& headers,
        std::optional<std::uint64_t> rangeStart,
        std::optional<std::wstring> const& validator,
        Http::Cancellation& cancellation)
    {
        URL_COMPONENTS parts{ .dwStructSize = sizeof(URL_COMPONENTS) };
        parts.dwSchemeLength = static_cast<DWORD>(-1);
        parts.dwHostNameLength = static_cast<DWORD>(-1);
        parts.dwUrlPathLength = static_cast<DWORD>(-1);
        parts.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &parts))
        {
            ThrowSystem(GetLastError(), "A download source URL could not be parsed");
        }
        std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
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

        Http::Response response;
        response.Session.reset(WinHttpOpen(
            L"Halo Desktop/1.0",
            WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0));
        if (!response.Session)
        {
            ThrowSystem(GetLastError(), "The download HTTP session could not be created");
        }
        if (!WinHttpSetTimeouts(response.Session.get(), 30000, 30000, 30000, 45000))
        {
            ThrowSystem(GetLastError(), "The download HTTP timeouts could not be configured");
        }
        response.Connection.reset(WinHttpConnect(response.Session.get(), host.c_str(), parts.nPort, 0));
        if (!response.Connection)
        {
            ThrowSystem(GetLastError(), "The download source could not be connected");
        }
        auto const flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
        response.Request.reset(WinHttpOpenRequest(
            response.Connection.get(),
            L"GET",
            target.c_str(),
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            flags));
        if (!response.Request)
        {
            ThrowSystem(GetLastError(), "The download request could not be created");
        }
        DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        if (!WinHttpSetOption(
            response.Request.get(),
            WINHTTP_OPTION_REDIRECT_POLICY,
            &redirectPolicy,
            sizeof(redirectPolicy)))
        {
            ThrowSystem(GetLastError(), "The download redirect policy could not be configured");
        }
        for (auto const& [key, value] : headers)
        {
            AddHeader(response.Request.get(), key + L": " + value, "A protected download header was rejected");
        }
        if (rangeStart)
        {
            AddHeader(response.Request.get(), L"Range: bytes=" + std::to_wstring(*rangeStart) + L"-",
                "The download range could not be applied");
            if (validator)
            {
                AddHeader(response.Request.get(), L"If-Range: " + *validator,
                    "The download validator could not be applied");
            }
        }

        if (!cancellation.Enter(response.Request.get()))
        {
            throw Http::RequestCancelled{};
        }
        auto const sent = WinHttpSendRequest(
            response.Request.get(),
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0)
            && WinHttpReceiveResponse(response.Request.get(), nullptr);
        auto const sendError = GetLastError();
        if (cancellation.Leave())
        {
            // Closed by Cancel: the handle is not ours to close again.
            static_cast<void>(response.Request.release());
            throw Http::RequestCancelled{};
        }
        if (!sent)
        {
            ThrowSystem(sendError, "The download response could not be received");
        }

        DWORD status{};
        DWORD statusBytes = sizeof(status);
        if (!WinHttpQueryHeaders(
            response.Request.get(),
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status,
            &statusBytes,
            WINHTTP_NO_HEADER_INDEX))
        {
            ThrowSystem(GetLastError(), "The download response status could not be read");
        }
        auto& head = response.Head;
        head.Status = status;
        head.ETag = QueryHeader(response.Request.get(), L"ETag");
        head.LastModified = QueryHeader(response.Request.get(), L"Last-Modified");
        head.Location = QueryHeader(response.Request.get(), L"Location");
        head.ContentRange = QueryHeader(response.Request.get(), L"Content-Range");
        head.ContentEncoding = QueryHeader(response.Request.get(), L"Content-Encoding");
        head.AcceptRanges = QueryHeader(response.Request.get(), L"Accept-Ranges");
        head.RetryAfter = QueryHeader(response.Request.get(), L"Retry-After");
        if (auto const length = QueryHeader(response.Request.get(), L"Content-Length"))
        {
            head.ContentLength = Http::ParseDecimal(*length);
        }
        return response;
    }
}

namespace HaloDesktop::Services::Downloads::Http
{
    void HandleCloser::operator()(void* value) const noexcept
    {
        if (value)
        {
            WinHttpCloseHandle(value);
        }
    }

    bool Cancellation::Enter(HINTERNET request) noexcept
    {
        std::scoped_lock const lock{ m_mutex };
        if (m_cancelled)
        {
            return false;
        }
        m_active = request;
        m_closedActive = false;
        return true;
    }

    bool Cancellation::Leave() noexcept
    {
        std::scoped_lock const lock{ m_mutex };
        m_active = nullptr;
        return std::exchange(m_closedActive, false);
    }

    void Cancellation::Cancel() noexcept
    {
        HINTERNET closing{};
        {
            std::scoped_lock const lock{ m_mutex };
            if (m_cancelled)
            {
                return;
            }
            m_cancelled = true;
            if (m_active)
            {
                closing = std::exchange(m_active, nullptr);
                m_closedActive = true;
            }
        }
        // Outside the lock: closing can take a moment, and Leave must not wait
        // on it. From here the caller of Leave never touches this handle again.
        if (closing)
        {
            WinHttpCloseHandle(closing);
        }
    }

    bool Cancellation::IsCancelled() const noexcept
    {
        std::scoped_lock const lock{ m_mutex };
        return m_cancelled;
    }

    Response OpenGet(
        std::wstring const& url,
        HeaderMap const& headers,
        std::optional<std::uint64_t> rangeStart,
        std::optional<std::wstring> const& validator,
        Cancellation& cancellation)
    {
        auto currentUrl = url;
        HeaderMap const emptyHeaders;
        bool forwardProtectedHeaders = true;
        bool crossedOrigin{};
        for (int redirectCount = 0;; ++redirectCount)
        {
            auto response = OpenGetOnce(
                currentUrl,
                forwardProtectedHeaders ? headers : emptyHeaders,
                rangeStart,
                validator,
                cancellation);
            if (!HaloDesktop::Security::IsRedirectStatus(response.Head.Status))
            {
                response.Head.CrossedOrigin = crossedOrigin;
                return response;
            }
            if (!response.Head.Location)
            {
                throw RedirectRejected{};
            }
            auto const target = HaloDesktop::Security::NextRedirectTarget(
                currentUrl, *response.Head.Location, redirectCount);
            if (!target)
            {
                throw RedirectRejected{};
            }
            if (!target->SameOrigin)
            {
                forwardProtectedHeaders = false;
                crossedOrigin = true;
            }
            currentUrl = target->Url;
        }
    }

    ReadOutcome ReadSome(
        Response& response,
        std::span<std::uint8_t> buffer,
        std::size_t& read,
        Cancellation& cancellation) noexcept
    {
        read = 0;
        if (!response.Request || !cancellation.Enter(response.Request.get()))
        {
            return ReadOutcome::Failed;
        }
        DWORD count{};
        auto const capacity = static_cast<DWORD>((std::min)(
            buffer.size(),
            static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
        auto const ok = WinHttpReadData(response.Request.get(), buffer.data(), capacity, &count);
        if (cancellation.Leave())
        {
            static_cast<void>(response.Request.release());
            return ReadOutcome::Failed;
        }
        if (!ok)
        {
            return ReadOutcome::Failed;
        }
        if (count == 0)
        {
            return ReadOutcome::End;
        }
        read = count;
        return ReadOutcome::Data;
    }

    std::optional<std::wstring> ValidatorOf(ResponseHead const& head)
    {
        return head.ETag ? head.ETag : head.LastModified;
    }

    std::optional<std::uint64_t> ParseDecimal(std::wstring_view value) noexcept
    {
        if (value.empty())
        {
            return std::nullopt;
        }
        std::uint64_t result{};
        for (auto const character : value)
        {
            if (character < L'0' || character > L'9')
            {
                return std::nullopt;
            }
            auto const digit = static_cast<std::uint64_t>(character - L'0');
            if (result > ((std::numeric_limits<std::uint64_t>::max)() - digit) / 10)
            {
                return std::nullopt;
            }
            result = result * 10 + digit;
        }
        return result;
    }

    std::optional<std::uint64_t> RetryAfterSeconds(ResponseHead const& head) noexcept
    {
        if (!head.RetryAfter)
        {
            return std::nullopt;
        }
        std::wstring_view value{ *head.RetryAfter };
        while (!value.empty() && (value.front() == L' ' || value.front() == L'\t')) value.remove_prefix(1);
        while (!value.empty() && (value.back() == L' ' || value.back() == L'\t')) value.remove_suffix(1);
        return ParseDecimal(value);
    }

    WatchOutcome RunWatched(
        std::function<void(Cancellation&)> const& work,
        std::function<bool()> const& cancelled,
        std::function<bool()> const& shutdown,
        std::function<bool()> const& limited,
        std::chrono::milliseconds poll,
        std::chrono::milliseconds limit)
    {
        Cancellation cancellation;
        std::mutex mutex;
        std::condition_variable finished;
        bool done{};
        auto const started = std::chrono::steady_clock::now();
        // Declared after everything it touches, so it is joined before they go.
        std::jthread worker{ [&work, &cancellation, &mutex, &finished, &done]()
        {
            work(cancellation);
            {
                std::scoped_lock const lock{ mutex };
                done = true;
            }
            finished.notify_all();
        } };
        auto outcome = WatchOutcome::Completed;
        for (;;)
        {
            {
                std::unique_lock lock{ mutex };
                if (finished.wait_for(lock, poll, [&done] { return done; }))
                {
                    break;
                }
            }
            // Checked outside the lock: these are the caller's code.
            if (shutdown())
            {
                outcome = WatchOutcome::Shutdown;
            }
            else if (cancelled())
            {
                outcome = WatchOutcome::Cancelled;
            }
            else if (limited() && std::chrono::steady_clock::now() - started >= limit)
            {
                outcome = WatchOutcome::TimedOut;
            }
            if (outcome != WatchOutcome::Completed)
            {
                break;
            }
        }
        if (outcome != WatchOutcome::Completed)
        {
            cancellation.Cancel();
        }
        worker.join();
        return outcome;
    }
}
