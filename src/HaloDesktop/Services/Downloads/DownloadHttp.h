#pragma once

#include <windows.h>
#include <winhttp.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// The download engine's HTTP: synchronous WinHTTP GETs that follow redirects by
// hand so protected headers never reach another origin, and a cancellation
// object that wakes a blocked call by closing its request handle from another
// thread. Used only by the engine and its segmented transfer; never UI-bound.
namespace HaloDesktop::Services::Downloads::Http
{
    struct HandleCloser final
    {
        void operator()(void* value) const noexcept;
    };
    using UniqueHandle = std::unique_ptr<void, HandleCloser>;

    // What a response said about itself, with no handle attached.
    struct ResponseHead final
    {
        std::uint32_t Status{};
        std::optional<std::wstring> ETag;
        std::optional<std::wstring> LastModified;
        std::optional<std::wstring> Location;
        std::optional<std::wstring> ContentRange;
        std::optional<std::wstring> ContentEncoding;
        std::optional<std::wstring> AcceptRanges;
        std::optional<std::wstring> RetryAfter;
        std::optional<std::uint64_t> ContentLength;
        bool CrossedOrigin{};
    };

    struct Response final
    {
        UniqueHandle Session;
        UniqueHandle Connection;
        UniqueHandle Request;
        ResponseHead Head;
    };

    // A redirect that must not be followed: no Location, too many hops, or a
    // step out of https.
    struct RedirectRejected final
    {
    };

    // The call was abandoned because Cancel ran.
    struct RequestCancelled final
    {
    };

    // Lets one thread abandon another thread's blocking WinHTTP call. The calling
    // thread brackets each blocking call with Enter and Leave; Cancel closes the
    // handle registered between them, which WinHTTP documents as the way to end a
    // pending synchronous call. A handle Cancel closed belongs to Cancel from
    // then on: Leave reports it, and the caller must release it, not close it.
    // Thread-safe. One instance serves one sequence of calls and is never reset.
    class Cancellation final
    {
    public:
        Cancellation() = default;
        Cancellation(Cancellation const&) = delete;
        Cancellation& operator=(Cancellation const&) = delete;

        // False once cancelled: the caller must not start the call.
        [[nodiscard]] bool Enter(HINTERNET request) noexcept;
        // True when Cancel closed the handle registered by Enter.
        [[nodiscard]] bool Leave() noexcept;
        void Cancel() noexcept;
        [[nodiscard]] bool IsCancelled() const noexcept;

    private:
        mutable std::mutex m_mutex;
        HINTERNET m_active{};
        bool m_cancelled{};
        bool m_closedActive{};
    };

    using HeaderMap = std::map<std::wstring, std::wstring, std::less<>>;

    // GET with the source's headers, following redirects by hand: at most five
    // hops, never out of https, and the protected headers stop at the first hop
    // that leaves the original origin. Throws RedirectRejected, RequestCancelled,
    // or std::system_error for a transport failure.
    [[nodiscard]] Response OpenGet(
        std::wstring const& url,
        HeaderMap const& headers,
        std::optional<std::uint64_t> rangeStart,
        std::optional<std::wstring> const& validator,
        Cancellation& cancellation);

    enum class ReadOutcome
    {
        Data,
        End,
        Failed,
    };

    // One WinHttpReadData into `buffer`. Failed covers a transport error and a
    // cancellation; after a cancellation the response's request handle is gone.
    [[nodiscard]] ReadOutcome ReadSome(
        Response& response,
        std::span<std::uint8_t> buffer,
        std::size_t& read,
        Cancellation& cancellation) noexcept;

    // ETag, else Last-Modified.
    [[nodiscard]] std::optional<std::wstring> ValidatorOf(ResponseHead const& head);
    [[nodiscard]] std::optional<std::uint64_t> ParseDecimal(std::wstring_view value) noexcept;
    // Retry-After as whole seconds. The HTTP-date form is not read.
    [[nodiscard]] std::optional<std::uint64_t> RetryAfterSeconds(ResponseHead const& head) noexcept;

    enum class WatchOutcome
    {
        Completed,
        Cancelled,
        TimedOut,
        Shutdown,
    };

    // Runs `work` on its own thread and waits for it, checking every `poll`
    // whether `cancelled` or `shutdown` says to stop, and while `limited` is
    // true whether `limit` has passed since the start. On any of those it cancels
    // the work's Cancellation, which wakes a blocked WinHTTP call, and joins the
    // thread before returning. `work` must not throw.
    [[nodiscard]] WatchOutcome RunWatched(
        std::function<void(Cancellation&)> const& work,
        std::function<bool()> const& cancelled,
        std::function<bool()> const& shutdown,
        std::function<bool()> const& limited,
        std::chrono::milliseconds poll,
        std::chrono::milliseconds limit);
}
