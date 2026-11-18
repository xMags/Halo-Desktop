#include "DownloadTransferTest.h"

#include "Services/Downloads/TransferEngine.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "Services/Downloads/DownloadHttp.h"
#include "Services/Downloads/SegmentPlanner.h"
#include "Services/Downloads/SegmentedTransfer.h"
#include "Storage/FileStorage.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include <winrt/Windows.Foundation.h>

namespace
{
    using HaloDesktop::Services::Downloads::DownloadFailureCode;
    using HaloDesktop::Services::Downloads::DownloadIndexStore;
    using HaloDesktop::Services::Downloads::DownloadRecord;
    using HaloDesktop::Services::Downloads::DownloadStartRequest;
    using HaloDesktop::Services::Downloads::DownloadStatus;
    using HaloDesktop::Services::Downloads::RequestVault;
    using HaloDesktop::Services::Downloads::SubtitleRequest;
    using HaloDesktop::Services::Downloads::TransferEngine;
    using HaloDesktop::Services::Downloads::MakeAccountKey;
    using HaloDesktop::Services::Downloads::Sha256Hex;

    constexpr auto TransferTimeout = std::chrono::seconds{ 10 };

    void Require(bool condition, char const* message)
    {
        if (!condition)
        {
            throw std::runtime_error{ message };
        }
    }

    class Apartment final
    {
    public:
        Apartment()
        {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        }

        ~Apartment()
        {
            winrt::uninit_apartment();
        }

        Apartment(Apartment const&) = delete;
        Apartment& operator=(Apartment const&) = delete;
    };

    class TemporaryDirectory final
    {
    public:
        TemporaryDirectory()
        {
            GUID id{};
            winrt::check_hresult(CoCreateGuid(&id));
            std::array<wchar_t, 40> value{};
            if (StringFromGUID2(id, value.data(), static_cast<int>(value.size())) == 0)
            {
                throw std::runtime_error{ "The transfer test could not create a temporary identifier." };
            }
            m_path = std::filesystem::temp_directory_path()
                / (std::wstring{ L"HaloDesktop-Stability-" } + value.data());
            if (!std::filesystem::create_directory(m_path))
            {
                throw std::runtime_error{ "The transfer test could not create its temporary root." };
            }
        }

        ~TemporaryDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(m_path, ignored);
        }

        TemporaryDirectory(TemporaryDirectory const&) = delete;
        TemporaryDirectory& operator=(TemporaryDirectory const&) = delete;

        [[nodiscard]] std::filesystem::path const& Path() const noexcept
        {
            return m_path;
        }

    private:
        std::filesystem::path m_path;
    };

    void SendAll(SOCKET socket, char const* data, std::size_t size)
    {
        std::size_t sent{};
        while (sent < size)
        {
            auto const remaining = (std::min)(
                size - sent,
                static_cast<std::size_t>((std::numeric_limits<int>::max)()));
            auto const count = send(socket, data + sent, static_cast<int>(remaining), 0);
            if (count <= 0)
            {
                throw std::runtime_error{ "The transfer test server could not send a response." };
            }
            sent += static_cast<std::size_t>(count);
        }
    }

    [[nodiscard]] std::string NarrowAscii(std::wstring const& value)
    {
        std::string result;
        result.reserve(value.size());
        for (auto const character : value)
        {
            if (character < 0 || character > 127)
            {
                throw std::invalid_argument{ "The loopback test URL is not ASCII." };
            }
            result.push_back(static_cast<char>(character));
        }
        return result;
    }

    class DownloadHttpServer final
    {
    public:
        explicit DownloadHttpServer(
            std::optional<std::wstring> redirectTarget = std::nullopt,
            std::uint8_t firstFill = static_cast<std::uint8_t>(0x31))
            : m_firstBody(384u * 1024u, firstFill),
              m_replacementBody(448u * 1024u, static_cast<std::uint8_t>(0x52)),
              m_redirectTarget(std::move(redirectTarget))
        {
            WSADATA data{};
            if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
            {
                throw std::runtime_error{ "Winsock could not start for the transfer test." };
            }
            m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (m_socket == INVALID_SOCKET)
            {
                WSACleanup();
                throw std::runtime_error{ "The transfer test socket could not be created." };
            }

            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port = 0;
            if (bind(m_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR
                || listen(m_socket, SOMAXCONN) == SOCKET_ERROR)
            {
                closesocket(m_socket);
                m_socket = INVALID_SOCKET;
                WSACleanup();
                throw std::runtime_error{ "The transfer test server could not listen." };
            }

            int addressSize = sizeof(address);
            if (getsockname(
                m_socket,
                reinterpret_cast<sockaddr*>(&address),
                &addressSize) == SOCKET_ERROR)
            {
                closesocket(m_socket);
                m_socket = INVALID_SOCKET;
                WSACleanup();
                throw std::runtime_error{ "The transfer test server port could not be read." };
            }
            m_port = ntohs(address.sin_port);
            m_thread = std::jthread([this]() { Run(); });
        }

        ~DownloadHttpServer()
        {
            m_stopping.store(true);
            WakeListener();
            if (m_thread.joinable())
            {
                m_thread.join();
            }
            if (m_socket != INVALID_SOCKET)
            {
                closesocket(m_socket);
                m_socket = INVALID_SOCKET;
            }
            WSACleanup();
        }

        DownloadHttpServer(DownloadHttpServer const&) = delete;
        DownloadHttpServer& operator=(DownloadHttpServer const&) = delete;

        [[nodiscard]] std::wstring FirstUrl() const
        {
            return BaseUrl() + L"/video-first";
        }

        [[nodiscard]] std::wstring ReplacementUrl() const
        {
            return BaseUrl() + L"/video-replacement";
        }

        [[nodiscard]] std::wstring SubtitleUrl() const
        {
            return BaseUrl() + L"/subtitle.srt";
        }

        [[nodiscard]] std::wstring ExpiringUrl() const
        {
            return BaseUrl() + L"/video-expiring";
        }

        [[nodiscard]] std::wstring CancelUrl() const
        {
            return BaseUrl() + L"/video-cancel";
        }

        [[nodiscard]] std::wstring RelativeRedirectUrl() const
        {
            return BaseUrl() + L"/video-relative-redirect";
        }

        [[nodiscard]] std::wstring CrossOriginRedirectUrl() const
        {
            return BaseUrl() + L"/video-cross-origin-redirect";
        }

        [[nodiscard]] std::wstring RedirectTargetUrl() const
        {
            return BaseUrl() + L"/video-redirect-target";
        }

        [[nodiscard]] std::wstring RedirectLoopUrl() const
        {
            return BaseUrl() + L"/video-redirect-loop";
        }

        [[nodiscard]] std::vector<std::uint8_t> const& FirstBody() const noexcept
        {
            return m_firstBody;
        }

        [[nodiscard]] std::vector<std::uint8_t> const& ReplacementBody() const noexcept
        {
            return m_replacementBody;
        }

        [[nodiscard]] int AuthorizedVideoRequests() const noexcept
        {
            return m_authorizedVideoRequests.load();
        }

        [[nodiscard]] int AuthorizedSubtitleRequests() const noexcept
        {
            return m_authorizedSubtitleRequests.load();
        }

        [[nodiscard]] int RejectedRequests() const noexcept
        {
            return m_rejectedRequests.load();
        }

        [[nodiscard]] int CrossOriginRedirectRequests() const noexcept
        {
            return m_crossOriginRedirectRequests.load();
        }

        [[nodiscard]] int RedirectTargetRequests() const noexcept
        {
            return m_redirectTargetRequests.load();
        }

        [[nodiscard]] int RedirectTargetRangeRequests() const noexcept
        {
            return m_redirectTargetRangeRequests.load();
        }

    private:
        [[nodiscard]] std::wstring BaseUrl() const
        {
            return L"http://127.0.0.1:" + std::to_wstring(m_port);
        }

        void WakeListener() const noexcept
        {
            auto const wake = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (wake == INVALID_SOCKET)
            {
                return;
            }
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port = htons(m_port);
            static_cast<void>(connect(
                wake,
                reinterpret_cast<sockaddr*>(&address),
                sizeof(address)));
            shutdown(wake, SD_BOTH);
            closesocket(wake);
        }

        void Run() noexcept
        {
            while (!m_stopping.load())
            {
                auto const client = accept(m_socket, nullptr, nullptr);
                if (client == INVALID_SOCKET)
                {
                    continue;
                }
                try
                {
                    Serve(client);
                }
                catch (...)
                {
                }
                shutdown(client, SD_BOTH);
                closesocket(client);
            }
        }

        void Serve(SOCKET client)
        {
            std::string request;
            std::array<char, 2048> buffer{};
            while (request.find("\r\n\r\n") == std::string::npos
                && request.size() < 64u * 1024u)
            {
                auto const count = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
                if (count <= 0)
                {
                    break;
                }
                request.append(buffer.data(), static_cast<std::size_t>(count));
            }

            auto const first = request.starts_with("GET /video-first ");
            auto const replacement = request.starts_with("GET /video-replacement ");
            auto const expiring = request.starts_with("GET /video-expiring ");
            auto const cancel = request.starts_with("GET /video-cancel ");
            auto const relativeRedirect = request.starts_with("GET /video-relative-redirect ");
            auto const crossOriginRedirect = request.starts_with("GET /video-cross-origin-redirect ");
            auto const redirectTarget = request.starts_with("GET /video-redirect-target ");
            auto const redirectLoop = request.starts_with("GET /video-redirect-loop ");
            auto const subtitle = request.starts_with("GET /subtitle.srt ");
            auto const hasVideoHeader =
                request.find("\r\nX-Halo-Test: video-secret\r\n") != std::string::npos;
            auto const hasRefreshedHeader =
                request.find("\r\nX-Halo-Test: refreshed-secret\r\n") != std::string::npos;
            auto const hasSubtitleHeader =
                request.find("\r\nX-Halo-Test: subtitle-secret\r\n") != std::string::npos;
            if ((first || replacement) && hasVideoHeader)
            {
                ++m_authorizedVideoRequests;
                SendBody(client, first ? m_firstBody : m_replacementBody, "application/octet-stream");
                return;
            }
            if (expiring && hasRefreshedHeader)
            {
                ++m_authorizedVideoRequests;
                SendBody(client, m_replacementBody, "application/octet-stream");
                return;
            }
            if (expiring && hasVideoHeader)
            {
                ++m_authorizedVideoRequests;
                static constexpr std::string_view response =
                    "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                SendAll(client, response.data(), response.size());
                return;
            }
            if (cancel && hasVideoHeader)
            {
                ++m_authorizedVideoRequests;
                SendBody(client, m_cancelBody, "application/octet-stream");
                return;
            }
            if (relativeRedirect && hasVideoHeader)
            {
                SendRedirect(client, "/video-first");
                return;
            }
            if (crossOriginRedirect && hasVideoHeader && m_redirectTarget)
            {
                ++m_crossOriginRedirectRequests;
                SendRedirect(client, NarrowAscii(*m_redirectTarget));
                return;
            }
            if (redirectTarget && !hasVideoHeader && !hasRefreshedHeader)
            {
                ++m_redirectTargetRequests;
                auto const range = ParseRange(request);
                if (range)
                {
                    ++m_redirectTargetRangeRequests;
                    SendPartialBody(client, m_firstBody, *range, "application/octet-stream");
                }
                else
                {
                    SendBody(client, m_firstBody, "application/octet-stream");
                }
                return;
            }
            if (redirectLoop && hasVideoHeader)
            {
                SendRedirect(client, "/video-redirect-loop");
                return;
            }
            if (subtitle && hasSubtitleHeader)
            {
                ++m_authorizedSubtitleRequests;
                static constexpr std::string_view body =
                    "1\n00:00:00,000 --> 00:00:01,000\nHalo stability test\n";
                SendBody(
                    client,
                    std::vector<std::uint8_t>{ body.begin(), body.end() },
                    "application/x-subrip");
                return;
            }

            ++m_rejectedRequests;
            static constexpr std::string_view response =
                "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            SendAll(client, response.data(), response.size());
        }

        void SendBody(
            SOCKET client,
            std::vector<std::uint8_t> const& body,
            std::string_view contentType)
        {
            auto const header =
                "HTTP/1.1 200 OK\r\nContent-Type: " + std::string{ contentType }
                + "\r\nContent-Length: " + std::to_string(body.size())
                + "\r\nETag: \"halo-stability\"\r\nConnection: close\r\n\r\n";
            SendAll(client, header.data(), header.size());
            constexpr std::size_t chunkSize = 16u * 1024u;
            for (std::size_t offset{}; offset < body.size(); offset += chunkSize)
            {
                auto const count = (std::min)(chunkSize, body.size() - offset);
                SendAll(
                    client,
                    reinterpret_cast<char const*>(body.data() + offset),
                    count);
                std::this_thread::sleep_for(std::chrono::milliseconds{ 4 });
            }
        }

        static std::optional<std::uint64_t> ParseRange(std::string const& request)
        {
            constexpr std::string_view prefix = "\r\nRange: bytes=";
            auto const start = request.find(prefix);
            if (start == std::string::npos)
            {
                return std::nullopt;
            }
            auto const valueStart = start + prefix.size();
            auto const dash = request.find('-', valueStart);
            if (dash == std::string::npos || dash == valueStart)
            {
                return std::nullopt;
            }
            std::uint64_t value{};
            for (auto index = valueStart; index < dash; ++index)
            {
                if (request[index] < '0' || request[index] > '9')
                {
                    return std::nullopt;
                }
                auto const digit = static_cast<std::uint64_t>(request[index] - '0');
                if (value > ((std::numeric_limits<std::uint64_t>::max)() - digit) / 10)
                {
                    return std::nullopt;
                }
                value = value * 10 + digit;
            }
            return value;
        }

        void SendPartialBody(
            SOCKET client,
            std::vector<std::uint8_t> const& body,
            std::uint64_t start,
            std::string_view contentType)
        {
            if (start >= body.size())
            {
                static constexpr std::string_view response =
                    "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                SendAll(client, response.data(), response.size());
                return;
            }
            auto const length = body.size() - static_cast<std::size_t>(start);
            auto const header =
                "HTTP/1.1 206 Partial Content\r\nContent-Type: " + std::string{ contentType }
                + "\r\nContent-Length: " + std::to_string(length)
                + "\r\nContent-Range: bytes " + std::to_string(start) + "-"
                + std::to_string(body.size() - 1) + "/" + std::to_string(body.size())
                + "\r\nConnection: close\r\n\r\n";
            SendAll(client, header.data(), header.size());
            SendAll(
                client,
                reinterpret_cast<char const*>(body.data() + static_cast<std::size_t>(start)),
                length);
        }

        void SendRedirect(SOCKET client, std::string const& location)
        {
            auto const response = "HTTP/1.1 302 Found\r\nLocation: " + location
                + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            SendAll(client, response.data(), response.size());
        }

        SOCKET m_socket{ INVALID_SOCKET };
        std::uint16_t m_port{};
        std::atomic_bool m_stopping{};
        std::atomic_int m_authorizedVideoRequests{};
        std::atomic_int m_authorizedSubtitleRequests{};
        std::atomic_int m_rejectedRequests{};
        std::atomic_int m_crossOriginRedirectRequests{};
        std::atomic_int m_redirectTargetRequests{};
        std::atomic_int m_redirectTargetRangeRequests{};
        std::vector<std::uint8_t> m_firstBody;
        std::vector<std::uint8_t> m_replacementBody;
        std::vector<std::uint8_t> m_cancelBody = std::vector<std::uint8_t>(
            8u * 1024u * 1024u,
            static_cast<std::uint8_t>(0x43));
        std::optional<std::wstring> m_redirectTarget;
        std::jthread m_thread;
    };

    [[nodiscard]] DownloadStartRequest MakeRequest(
        std::wstring url,
        std::wstring fileName,
        std::uint64_t videoSize,
        bool replaceExisting,
        std::optional<SubtitleRequest> subtitle = std::nullopt,
        std::wstring videoId = L"movie:transfer-stability",
        std::wstring headerValue = L"video-secret")
    {
        return {
            .Media = {
                .VideoId = videoId,
                .ItemId = videoId,
                .MediaType = L"movie",
                .Title = L"Transfer stability",
                .FileName = std::move(fileName),
                .VideoSize = videoSize,
            },
            .Request = {
                .Url = std::move(url),
                .Headers = { { L"X-Halo-Test", std::move(headerValue) } },
                .Subtitle = std::move(subtitle),
            },
            .ReplaceExisting = replaceExisting,
        };
    }

    void WaitForFile(std::filesystem::path const& path)
    {
        auto const deadline = std::chrono::steady_clock::now() + TransferTimeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            std::error_code error;
            if (std::filesystem::file_size(path, error) > 0 && !error)
            {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{ 20 });
        }
        throw std::runtime_error{ "The transfer test timed out waiting for a partial file." };
    }

    [[nodiscard]] DownloadRecord WaitForStatus(
        TransferEngine& engine,
        std::wstring const& jobId,
        DownloadStatus status)
    {
        auto const deadline = std::chrono::steady_clock::now() + TransferTimeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            auto const records = engine.List();
            auto const found = std::find_if(records.begin(), records.end(), [&jobId](auto const& record)
            {
                return record.JobId == jobId;
            });
            if (found != records.end() && found->Status == status)
            {
                return *found;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{ 20 });
        }
        throw std::runtime_error{ "The transfer test timed out waiting for download state." };
    }

    [[nodiscard]] std::vector<std::uint8_t> ReadBytes(std::filesystem::path const& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            throw std::runtime_error{ "The transfer test could not read a downloaded file." };
        }
        return {
            std::istreambuf_iterator<char>{ input },
            std::istreambuf_iterator<char>{},
        };
    }

    void WriteBytes(std::filesystem::path const& path, std::vector<std::uint8_t> const& bytes)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error{ "The transfer test could not create a partial file." };
        }
        output.write(
            reinterpret_cast<char const*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        if (!output)
        {
            throw std::runtime_error{ "The transfer test could not write a partial file." };
        }
    }

    // Opened sharing delete as well as read and write: the engine replaces the
    // index by renaming a new file over it, and a reader that denies delete
    // would make that rename fail while a test polls the file.
    [[nodiscard]] std::string ReadText(std::filesystem::path const& path)
    {
        winrt::file_handle const file{ CreateFileW(
            path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr) };
        if (!file)
        {
            throw std::runtime_error{ "The transfer test could not read the download index." };
        }
        std::string text;
        char buffer[4096];
        for (;;)
        {
            DWORD read{};
            if (!ReadFile(file.get(), buffer, sizeof(buffer), &read, nullptr))
            {
                throw std::runtime_error{ "The transfer test could not read the download index." };
            }
            if (read == 0)
            {
                return text;
            }
            text.append(buffer, read);
        }
    }

    template <typename Predicate>
    void WaitUntil(Predicate const& done, char const* message)
    {
        auto const deadline = std::chrono::steady_clock::now() + TransferTimeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (done())
            {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{ 20 });
        }
        throw std::runtime_error{ message };
    }

    [[nodiscard]] bool ListsJob(TransferEngine& engine, std::wstring const& jobId)
    {
        auto const records = engine.List();
        return std::any_of(records.begin(), records.end(), [&jobId](DownloadRecord const& record)
        {
            return record.JobId == jobId;
        });
    }

    [[nodiscard]] std::filesystem::path TablePathOf(DownloadRecord const& record)
    {
        return HaloDesktop::Services::Downloads::Segments::TablePath(record.PartialPath());
    }
}

// The multi-connection transfer against a configurable loopback file server:
// the reference client's `downloads/segmented.rs` tests mirrored, plus the
// waits, refusals and engine rules this package added. Every finished file is
// compared byte for byte with what the server holds.
namespace
{
    namespace Downloads = HaloDesktop::Services::Downloads;
    namespace Segments = HaloDesktop::Services::Downloads::Segments;
    using Downloads::SegmentedFinished;
    using Downloads::SegmentedHost;
    using Downloads::SegmentedJob;
    using Downloads::SegmentedOptions;
    using Downloads::SegmentedProgress;
    using Downloads::TransferEngineOptions;
    using Downloads::TransferFailure;
    using Downloads::TransferFailureKind;

    constexpr char const* EtagValue = "\"v1\"";
    constexpr wchar_t const* WideEtag = L"\"v1\"";

    // A pattern in which no two nearby offsets agree by accident.
    [[nodiscard]] std::vector<std::uint8_t> Content(std::size_t length)
    {
        std::uint32_t state = 0x2545f491u;
        std::vector<std::uint8_t> result(length);
        for (auto& value : result)
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            value = static_cast<std::uint8_t>(state >> 24);
        }
        return result;
    }

    struct LoggedRequest final
    {
        std::optional<std::uint64_t> Range;
        std::optional<std::string> IfRange;
        int Status{};
    };

    struct FileServerOptions final
    {
        std::vector<std::uint8_t> Body;
        std::optional<std::string> ETag{ EtagValue };
        // Content-Length on a whole-file answer.
        bool StateSize{ true };
        // False answers 200 with the whole file to any range.
        bool HonourRanges{ true };
        // Answers a ranged 206 whatever If-Range says.
        bool IgnoreIfRange{};
        std::optional<std::string> AcceptRanges;
        std::optional<std::string> ContentEncoding;
        // Any connection past this many at once is refused with RefuseStatus.
        std::size_t Limit{};
        int RefuseStatus{ 503 };
        std::optional<int> RetryAfter;
        // Ranged answers come from a different object under the same ETag.
        bool Inconsistent{};
        // Pause between 16 KiB writes.
        std::chrono::milliseconds Pace{};
        // How long a ranged request waits for its answer.
        std::chrono::milliseconds RangedDelay{};
        // How long every request waits for its answer.
        std::chrono::milliseconds ResponseDelay{};
        // ResponseDelay applies to the first request only.
        bool DelayFirstOnly{};
        // The first ranged answer sends a little and then nothing more.
        bool StallFirstRanged{};
        // Answers every request with a redirect here instead.
        std::optional<std::string> RedirectTo;
    };

    // A plain HTTP/1.1 file server on loopback, one thread per connection, that
    // logs every request and counts how many connections it serves at once.
    class FileServer final
    {
    public:
        explicit FileServer(FileServerOptions options)
            : m_options(std::move(options))
        {
            WSADATA data{};
            if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
            {
                throw std::runtime_error{ "Winsock could not start for the file server." };
            }
            m_listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            int addressSize = sizeof(address);
            if (m_listener == INVALID_SOCKET
                || bind(m_listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR
                || listen(m_listener, SOMAXCONN) == SOCKET_ERROR
                || getsockname(m_listener, reinterpret_cast<sockaddr*>(&address), &addressSize) == SOCKET_ERROR)
            {
                if (m_listener != INVALID_SOCKET) closesocket(m_listener);
                WSACleanup();
                throw std::runtime_error{ "The file server could not listen." };
            }
            m_port = ntohs(address.sin_port);
            m_acceptor = std::thread{ [this]() { Accept(); } };
        }

        ~FileServer()
        {
            {
                std::scoped_lock const lock{ m_mutex };
                m_stopping = true;
                for (auto const client : m_clients)
                {
                    shutdown(client, SD_BOTH);
                }
            }
            m_stopped.notify_all();
            closesocket(m_listener);
            if (m_acceptor.joinable()) m_acceptor.join();
            std::vector<std::thread> workers;
            {
                std::scoped_lock const lock{ m_mutex };
                workers = std::move(m_workers);
            }
            for (auto& worker : workers)
            {
                if (worker.joinable()) worker.join();
            }
            WSACleanup();
        }

        FileServer(FileServer const&) = delete;
        FileServer& operator=(FileServer const&) = delete;

        [[nodiscard]] std::wstring Url(std::wstring_view path = L"/movie.mkv") const
        {
            return L"http://127.0.0.1:" + std::to_wstring(m_port) + std::wstring{ path };
        }

        [[nodiscard]] std::vector<std::uint8_t> const& Body() const noexcept
        {
            return m_options.Body;
        }

        [[nodiscard]] std::vector<LoggedRequest> Log() const
        {
            std::scoped_lock const lock{ m_mutex };
            return m_log;
        }

        [[nodiscard]] std::size_t Requests() const
        {
            std::scoped_lock const lock{ m_mutex };
            return m_log.size();
        }

        [[nodiscard]] std::size_t MaxActive() const noexcept
        {
            return m_maxActive.load();
        }

        // Answers every later request with this status and no body; 0 serves again.
        void ForceStatus(int status) noexcept
        {
            m_forcedStatus.store(status);
        }

    private:
        void Accept()
        {
            for (;;)
            {
                auto const client = accept(m_listener, nullptr, nullptr);
                std::scoped_lock const lock{ m_mutex };
                if (client == INVALID_SOCKET || m_stopping)
                {
                    if (client != INVALID_SOCKET) closesocket(client);
                    return;
                }
                m_clients.push_back(client);
                m_workers.emplace_back([this, client]()
                {
                    try
                    {
                        Serve(client);
                    }
                    catch (...)
                    {
                    }
                    std::scoped_lock const done{ m_mutex };
                    std::erase(m_clients, client);
                    shutdown(client, SD_BOTH);
                    closesocket(client);
                });
            }
        }

        // False once the server is stopping.
        [[nodiscard]] bool Wait(std::chrono::milliseconds duration)
        {
            std::unique_lock lock{ m_mutex };
            return !m_stopped.wait_for(lock, duration, [this] { return m_stopping; });
        }

        void Send(SOCKET client, std::string_view data)
        {
            std::size_t sent{};
            while (sent < data.size())
            {
                auto const count = send(client, data.data() + sent, static_cast<int>((std::min)(data.size() - sent, std::size_t{ 1 << 20 })), 0);
                if (count <= 0)
                {
                    throw std::runtime_error{ "The client went away." };
                }
                sent += static_cast<std::size_t>(count);
            }
        }

        void Serve(SOCKET client)
        {
            std::string request;
            std::array<char, 2048> buffer{};
            while (request.find("\r\n\r\n") == std::string::npos && request.size() < 64u * 1024u)
            {
                auto const count = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
                if (count <= 0)
                {
                    return;
                }
                request.append(buffer.data(), static_cast<std::size_t>(count));
            }
            std::optional<std::uint64_t> range;
            std::optional<std::string> ifRange;
            std::size_t lineStart = request.find("\r\n");
            while (lineStart != std::string::npos && lineStart + 2 < request.size())
            {
                auto const lineEnd = request.find("\r\n", lineStart + 2);
                if (lineEnd == std::string::npos) break;
                auto const line = request.substr(lineStart + 2, lineEnd - lineStart - 2);
                lineStart = lineEnd;
                auto const colon = line.find(':');
                if (colon == std::string::npos) continue;
                auto name = line.substr(0, colon);
                std::transform(name.begin(), name.end(), name.begin(), [](char character)
                {
                    return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
                });
                auto value = line.substr(colon + 1);
                while (!value.empty() && value.front() == ' ') value.erase(value.begin());
                if (name == "range" && value.starts_with("bytes=") && value.ends_with("-"))
                {
                    range = std::stoull(value.substr(6, value.size() - 7));
                }
                else if (name == "if-range")
                {
                    ifRange = value;
                }
            }

            if (m_options.RedirectTo)
            {
                Record(LoggedRequest{ range, ifRange, 302 });
                Send(client, "HTTP/1.1 302 Found\r\nLocation: " + *m_options.RedirectTo
                    + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                return;
            }
            auto const delayed = m_options.ResponseDelay.count() > 0
                && (!m_options.DelayFirstOnly || !m_delayedOnce.exchange(true));
            if (delayed && !Wait(m_options.ResponseDelay))
            {
                return;
            }
            if (range && m_options.RangedDelay.count() > 0 && !Wait(m_options.RangedDelay))
            {
                return;
            }
            auto const active = ++m_active;
            auto const leave = std::unique_ptr<std::atomic_size_t, void (*)(std::atomic_size_t*)>{
                &m_active, [](std::atomic_size_t* value) { --*value; } };
            auto const forced = m_forcedStatus.load();
            auto const refused = forced != 0 || (m_options.Limit > 0 && active > m_options.Limit);
            int status{};
            if (refused)
            {
                status = forced != 0 ? forced : m_options.RefuseStatus;
            }
            else
            {
                auto observed = m_maxActive.load();
                while (active > observed && !m_maxActive.compare_exchange_weak(observed, active))
                {
                }
                auto const honours = m_options.HonourRanges
                    && (m_options.IgnoreIfRange || !ifRange || ifRange == m_options.ETag);
                status = range && honours ? 206 : 200;
            }
            Record(LoggedRequest{ range, ifRange, status });
            if (refused)
            {
                std::string head = "HTTP/1.1 " + std::to_string(status) + " Refused\r\n";
                if (m_options.RetryAfter) head += "Retry-After: " + std::to_string(*m_options.RetryAfter) + "\r\n";
                head += "Content-Length: 0\r\nConnection: close\r\n\r\n";
                Send(client, head);
                return;
            }
            auto const total = m_options.Body.size();
            auto const start = status == 206 ? static_cast<std::size_t>(*range) : std::size_t{};
            if (start >= total && total > 0)
            {
                Send(client, "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                return;
            }
            std::string head = status == 206 ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
            if (status == 206)
            {
                head += "Content-Range: bytes " + std::to_string(start) + "-" + std::to_string(total - 1)
                    + "/" + std::to_string(total) + "\r\n";
            }
            if (status == 206 || m_options.StateSize)
            {
                head += "Content-Length: " + std::to_string(total - start) + "\r\n";
            }
            if (m_options.ETag) head += "ETag: " + *m_options.ETag + "\r\n";
            if (m_options.AcceptRanges) head += "Accept-Ranges: " + *m_options.AcceptRanges + "\r\n";
            if (m_options.ContentEncoding) head += "Content-Encoding: " + *m_options.ContentEncoding + "\r\n";
            head += "Connection: close\r\n\r\n";
            Send(client, head);
            auto const flip = status == 206 && m_options.Inconsistent;
            auto const stall = status == 206 && m_options.StallFirstRanged && !m_stalledOnce.exchange(true);
            constexpr std::size_t ChunkSize = 16u * 1024u;
            for (auto offset = start; offset < total; offset += ChunkSize)
            {
                auto const count = (std::min)(ChunkSize, total - offset);
                std::string chunk(reinterpret_cast<char const*>(m_options.Body.data() + offset), count);
                if (flip)
                {
                    for (auto& character : chunk) character = static_cast<char>(character ^ 0xff);
                }
                Send(client, chunk);
                if (stall && offset - start >= 2 * ChunkSize)
                {
                    static_cast<void>(Wait(std::chrono::minutes{ 5 }));
                    return;
                }
                if (m_options.Pace.count() > 0 && !Wait(m_options.Pace))
                {
                    return;
                }
            }
        }

        void Record(LoggedRequest entry)
        {
            std::scoped_lock const lock{ m_mutex };
            m_log.push_back(std::move(entry));
        }

        FileServerOptions m_options;
        SOCKET m_listener{ INVALID_SOCKET };
        std::uint16_t m_port{};
        mutable std::mutex m_mutex;
        std::condition_variable m_stopped;
        bool m_stopping{};
        std::vector<SOCKET> m_clients;
        std::vector<std::thread> m_workers;
        std::vector<LoggedRequest> m_log;
        std::atomic_size_t m_active{};
        std::atomic_size_t m_maxActive{};
        std::atomic_int m_forcedStatus{};
        std::atomic_bool m_stalledOnce{};
        std::atomic_bool m_delayedOnce{};
        std::thread m_acceptor;
    };

    class FakeHost final
    {
    public:
        [[nodiscard]] SegmentedHost Host()
        {
            return SegmentedHost{
                .ResponseMetadata = [this](std::optional<std::wstring> const& validator, std::uint64_t total)
                {
                    std::scoped_lock const lock{ m_mutex };
                    m_metadata = std::pair{ validator, total };
                },
                .Progress = [this](SegmentedProgress const& progress)
                {
                    auto observed = m_mostConnections.load();
                    while (progress.Connections > observed
                        && !m_mostConnections.compare_exchange_weak(observed, progress.Connections))
                    {
                    }
                },
            };
        }

        [[nodiscard]] std::optional<std::pair<std::optional<std::wstring>, std::uint64_t>> Metadata() const
        {
            std::scoped_lock const lock{ m_mutex };
            return m_metadata;
        }

        [[nodiscard]] std::uint32_t MostConnections() const noexcept
        {
            return m_mostConnections.load();
        }

    private:
        mutable std::mutex m_mutex;
        std::optional<std::pair<std::optional<std::wstring>, std::uint64_t>> m_metadata;
        std::atomic_uint32_t m_mostConnections{};
    };

    [[nodiscard]] SegmentedOptions Quick()
    {
        SegmentedOptions options;
        options.Tuning.MaxConnections = Segments::MaxConnections;
        options.Tuning.MinSplitBytes = 256u * 1024u;
        options.Tuning.RampInterval = std::chrono::milliseconds{ 20 };
        options.Timeouts.CancelPoll = std::chrono::milliseconds{ 50 };
        return options;
    }

    class Fixture final
    {
    public:
        explicit Fixture(std::wstring url)
            : m_url(std::move(url))
        {
            std::filesystem::create_directories(Root());
        }

        [[nodiscard]] std::filesystem::path Root() const
        {
            return m_directory.Path() / L"downloads";
        }

        [[nodiscard]] std::filesystem::path Part() const
        {
            return Root() / L"movie.mkv.part";
        }

        [[nodiscard]] std::filesystem::path Table() const
        {
            return Segments::TablePath(Part());
        }

        [[nodiscard]] SegmentedJob Job(std::uint64_t knownTotal = 0, std::optional<std::wstring> validator = std::nullopt) const
        {
            return SegmentedJob{
                .Url = m_url,
                .Part = Part(),
                .Root = Root(),
                .KnownTotal = knownTotal,
                .Validator = std::move(validator),
            };
        }

        [[nodiscard]] bool PartMatches(std::vector<std::uint8_t> const& body) const
        {
            std::error_code error;
            return std::filesystem::is_regular_file(Part(), error) && ReadBytes(Part()) == body;
        }

    private:
        TemporaryDirectory m_directory;
        std::wstring m_url;
    };

    struct Outcome final
    {
        std::optional<SegmentedFinished> Finished;
        std::optional<TransferFailure> Failure;
    };

    [[nodiscard]] Outcome Run(
        SegmentedJob const& job,
        std::atomic_bool& cancel,
        FakeHost& host,
        SegmentedOptions const& options)
    {
        try
        {
            return Outcome{ Downloads::RunSegmentedTransfer(job, cancel, std::stop_token{}, host.Host(), options), std::nullopt };
        }
        catch (TransferFailure const& failure)
        {
            return Outcome{ std::nullopt, failure };
        }
    }

    [[nodiscard]] bool Failed(Outcome const& outcome, TransferFailureKind kind, DownloadFailureCode code)
    {
        return outcome.Failure && outcome.Failure->Kind == kind && outcome.Failure->Code == code;
    }

    void SeveralConnectionsDeliverTheExactFile()
    {
        FileServer server{ FileServerOptions{ .Body = Content(6u * 1024u * 1024u), .Pace = std::chrono::milliseconds{ 2 } } };
        Fixture fixture{ server.Url() };
        std::atomic_bool cancel{};
        FakeHost host;
        auto const outcome = Run(fixture.Job(), cancel, host, Quick());
        Require(outcome.Finished.has_value(), "a multi-connection transfer did not finish");
        Require(outcome.Finished->Total == server.Body().size(), "the finished total is wrong");
        Require(outcome.Finished->Validator == std::optional<std::wstring>{ WideEtag }, "the finished validator is wrong");
        Require(fixture.PartMatches(server.Body()), "the part differs from the source");
        Require(!std::filesystem::exists(fixture.Table()), "the table outlived the download");
        Require(server.MaxActive() > 1, "only one connection ran");
        auto const log = server.Log();
        Require(!log[0].Range, "the first request asked for a range");
        for (std::size_t index = 1; index < log.size(); ++index)
        {
            Require(log[index].Range && *log[index].Range > 0, "an extra connection did not ask for a later range");
            Require(log[index].IfRange == std::optional<std::string>{ EtagValue }, "an extra connection did not send If-Range");
            Require(log[index].Status == 206, "an extra connection was not answered with a range");
        }
    }

    // Progress counts the connections receiving data, which is what the
    // Downloads page shows, and never more than a run may open.
    void ProgressReportsHowManyConnectionsAreReceiving()
    {
        FileServer server{ FileServerOptions{ .Body = Content(2u * 1024u * 1024u), .Pace = std::chrono::milliseconds{ 15 } } };
        Fixture fixture{ server.Url() };
        std::atomic_bool cancel{};
        FakeHost host;
        auto const outcome = Run(fixture.Job(), cancel, host, Quick());
        Require(outcome.Finished.has_value() && fixture.PartMatches(server.Body()), "a paced transfer did not deliver the file");
        Require(host.MostConnections() > 1, "progress never counted more than one receiving connection");
        Require(host.MostConnections() <= Segments::MaxConnections, "progress counted more connections than a run may open");
    }

    void ASourceThatIgnoresRangesIsReadThroughOnOneConnection()
    {
        FileServer server{ FileServerOptions{
            .Body = Content(2u * 1024u * 1024u), .HonourRanges = false, .Pace = std::chrono::milliseconds{ 2 } } };
        Fixture fixture{ server.Url() };
        std::atomic_bool cancel{};
        FakeHost host;
        auto const outcome = Run(fixture.Job(), cancel, host, Quick());
        Require(outcome.Finished.has_value() && fixture.PartMatches(server.Body()), "a source without ranges was not read through");
        // The first request and one probe; its 200 ends the asking.
        Require(server.Requests() == 2, "a source that ignores ranges was asked more than once more");
        Require(!std::filesystem::exists(fixture.Table()), "the table outlived the download");
    }

    // A resolver's "not ready yet" clip is a whole, valid, tiny file. Against a
    // film-sized stream it is refused; with no size to judge by, or a small
    // stated video, the same answer is an ordinary download.
    void AStandInClipIsNotTakenForTheVideo()
    {
        constexpr std::uint64_t FilmBytes = 90ull * 1024ull * 1024ull * 1024ull;
        Require(Downloads::IsStandInForVideo(148'989, FilmBytes), "a stand-in clip against a film was accepted");
        Require(!Downloads::IsStandInForVideo(148'989, std::nullopt), "a small file with no stated size was refused");
        Require(!Downloads::IsStandInForVideo(148'989, 20ull * 1024ull * 1024ull), "a small file of a small video was refused");
        Require(!Downloads::IsStandInForVideo(600ull * 1024ull * 1024ull, FilmBytes), "an episode of a season pack was refused");
        Require(!Downloads::IsStandInForVideo(0, FilmBytes), "an unstated total was judged");

        FileServer server{ FileServerOptions{ .Body = Content(148'989) } };
        {
            Fixture fixture{ server.Url() };
            std::atomic_bool cancel{};
            FakeHost host;
            auto job = fixture.Job();
            job.SizeHint = FilmBytes;
            auto const outcome = Run(job, cancel, host, Quick());
            Require(Failed(outcome, TransferFailureKind::Permanent, DownloadFailureCode::SourceNotReady),
                "a stand-in clip finished as the video");
            Require(!fixture.PartMatches(server.Body()), "the stand-in clip was kept as the part");
        }
        {
            Fixture fixture{ server.Url() };
            std::atomic_bool cancel{};
            FakeHost host;
            auto const outcome = Run(fixture.Job(), cancel, host, Quick());
            Require(outcome.Finished.has_value() && fixture.PartMatches(server.Body()),
                "a small file with no stated size was not downloaded");
        }
    }

    // The first connection reaches the split point long before the probe is
    // answered, so it must wait there instead of stopping: when the answer is a
    // 206 it stops after the overlap, and when it is a 200 it takes the range
    // back and reads on.
    void TheFirstConnectionWaitsAtTheSplitForTheProbesAnswer()
    {
        for (auto const honours : { true, false })
        {
            FileServer server{ FileServerOptions{
                .Body = Content(2u * 1024u * 1024u),
                .HonourRanges = honours,
                .Pace = std::chrono::milliseconds{ 1 },
                .RangedDelay = std::chrono::milliseconds{ 600 },
            } };
            Fixture fixture{ server.Url() };
            std::atomic_bool cancel{};
            FakeHost host;
            auto options = Quick();
            options.Tuning.MaxConnections = 2;
            auto const outcome = Run(fixture.Job(), cancel, host, options);
            Require(outcome.Finished.has_value() && fixture.PartMatches(server.Body()), "a parked connection lost the file");
            if (!honours)
            {
                Require(server.Requests() == 2, "a parked connection's range was asked for again");
            }
            Require(!std::filesystem::exists(fixture.Table()), "the table outlived the download");
        }
    }

    void ARefusedProbeKeepsTheRunOnOneConnection()
    {
        FileServer server{ FileServerOptions{
            .Body = Content(2u * 1024u * 1024u), .Limit = 1, .RetryAfter = 0, .Pace = std::chrono::milliseconds{ 2 } } };
        Fixture fixture{ server.Url() };
        std::atomic_bool cancel{};
        FakeHost host;
        auto const outcome = Run(fixture.Job(), cancel, host, Quick());
        Require(outcome.Finished.has_value() && fixture.PartMatches(server.Body()), "a refused probe lost the file");
        Require(server.MaxActive() == 1, "more than one connection was served");
        Require(server.Requests() == 2, "the ceiling did not stop further asking");
    }

    void CountRefusals(FileServerOptions options, int refusal, char const* lost, char const* never, char const* many)
    {
        options.Body = Content(6u * 1024u * 1024u);
        options.Limit = 2;
        options.RefuseStatus = refusal;
        options.Pace = std::chrono::milliseconds{ 2 };
        FileServer server{ std::move(options) };
        Fixture fixture{ server.Url() };
        std::atomic_bool cancel{};
        FakeHost host;
        // The ramp is by the clock alone, so with the 20 ms step every
        // connection can already be asking when the first refusal sets the
        // ceiling, and each of those is refused too. A step long enough for an
        // answer to arrive first keeps the count about the ceiling.
        auto transfer = Quick();
        transfer.Tuning.RampInterval = std::chrono::milliseconds{ 100 };
        auto const outcome = Run(fixture.Job(), cancel, host, transfer);
        Require(outcome.Finished.has_value() && fixture.PartMatches(server.Body()), lost);
        auto const log = server.Log();
        auto const refused = std::count_if(log.begin(), log.end(), [refusal](LoggedRequest const& entry)
        {
            return entry.Status == refusal;
        });
        Require(refused >= 1, never);
        // Without the ceiling every ramp step would ask again and be refused.
        Require(refused <= 4, many);
    }

    void ARefusalAfterRangesAreProvenSetsTheCeiling()
    {
        CountRefusals(FileServerOptions{ .RetryAfter = 0 }, 503,
            "a 503 refusal lost the file", "the server never refused with 503", "a 503 ceiling was not kept");
        // A 429 without Retry-After waits the planner's own back-off instead.
        CountRefusals(FileServerOptions{}, 429,
            "a 429 refusal lost the file", "the server never refused with 429", "a 429 ceiling was not kept");
    }

    void A403ToAnExtraConnectionIsALimitNotAnExpiredLink()
    {
        CountRefusals(FileServerOptions{ .RetryAfter = 0 }, 403,
            "a 403 to an extra connection ended the download", "the server never refused with 403", "a 403 ceiling was not kept");
        CountRefusals(FileServerOptions{ .RetryAfter = 0 }, 404,
            "a 404 to an extra connection ended the download", "the server never refused with 404", "a 404 ceiling was not kept");
    }

    void DifferentBytesFromTwoConnectionsFailAndLeaveNothing()
    {
        FileServer server{ FileServerOptions{
            .Body = Content(2u * 1024u * 1024u), .Inconsistent = true, .Pace = std::chrono::milliseconds{ 2 } } };
        Fixture fixture{ server.Url() };
        std::atomic_bool cancel{};
        FakeHost host;
        auto const outcome = Run(fixture.Job(), cancel, host, Quick());
        Require(Failed(outcome, TransferFailureKind::Permanent, DownloadFailureCode::InvalidRange),
            "two different objects under one validator were not refused");
        Require(!std::filesystem::exists(fixture.Part()), "the corrupt part was kept");
        Require(!std::filesystem::exists(fixture.Table()), "the corrupt part's table was kept");
    }

    void ASourceWithoutAValidatorStaysOnOneConnection()
    {
        FileServer server{ FileServerOptions{
            .Body = Content(2u * 1024u * 1024u), .ETag = std::nullopt, .Pace = std::chrono::milliseconds{ 1 } } };
        Fixture fixture{ server.Url() };
        std::atomic_bool cancel{};
        FakeHost host;
        auto const outcome = Run(fixture.Job(), cancel, host, Quick());
        Require(outcome.Finished.has_value() && fixture.PartMatches(server.Body()), "a source without a validator lost the file");
        Require(server.Requests() == 1, "a source without a validator was asked for ranges");
        Require(!std::filesystem::exists(fixture.Table()), "a table was written without a validator");
    }

    // Not in the reference's tests, but its gate: an unstated size, an encoded
    // body or Accept-Ranges: none keeps the run on the one request.
    void AnyUnmetConditionKeepsTheRunOnOneConnection()
    {
        std::vector<FileServerOptions> cases;
        cases.push_back(FileServerOptions{ .StateSize = false });
        cases.push_back(FileServerOptions{ .AcceptRanges = std::string{ "None" } });
        cases.push_back(FileServerOptions{ .ContentEncoding = std::string{ "br" } });
        for (auto& options : cases)
        {
            options.Body = Content(2u * 1024u * 1024u);
            options.Pace = std::chrono::milliseconds{ 1 };
            FileServer server{ std::move(options) };
            Fixture fixture{ server.Url() };
            std::atomic_bool cancel{};
            FakeHost host;
            auto const outcome = Run(fixture.Job(), cancel, host, Quick());
            Require(outcome.Finished.has_value() && fixture.PartMatches(server.Body()), "a single-connection source lost the file");
            Require(outcome.Finished->Total == server.Body().size(), "an unstated size was not taken from the body");
            Require(server.Requests() == 1, "a source that does not qualify was asked for ranges");
        }
    }

    // A source that states no size ends where its body ends. The addon's size
    // hint, larger or smaller than the real file, must not hold it back.
    void ASizeHintIsNeverTheLengthOfAnUnstatedBody()
    {
        auto const body = Content(1024u * 1024u);
        for (auto const hint : { std::uint64_t{ 3u * 1024u * 1024u }, std::uint64_t{ 512u * 1024u } })
        {
            FileServer server{ FileServerOptions{ .Body = body, .StateSize = false, .Pace = std::chrono::milliseconds{ 1 } } };
            Fixture fixture{ server.Url() };
            std::atomic_bool cancel{};
            FakeHost host;
            auto job = fixture.Job();
            job.SizeHint = hint;
            auto const outcome = Run(job, cancel, host, Quick());
            Require(outcome.Finished.has_value() && fixture.PartMatches(server.Body()),
                "a body of unstated size was held to the addon's size hint");
            Require(outcome.Finished->Total == server.Body().size(), "an unstated size was not taken from the body");
            auto const metadata = host.Metadata();
            Require(metadata.has_value() && metadata->second == 0, "the size hint was reported as the source's size");
        }
    }

    // A part without a table is trusted only up to the last count stored
    // after a flush, and cut back to it; a count of 0, or one the part does not
    // reach, leaves the length in charge as before.
    void APartIsTrustedOnlyUpToItsFlushedCount()
    {
        FileServer server{ FileServerOptions{ .Body = Content(1024u * 1024u) } };
        auto options = Quick();
        options.Tuning.MaxConnections = 1;
        {
            Fixture fixture{ server.Url() };
            // What a power cut can leave: the length grew, the last bytes never landed.
            std::vector<std::uint8_t> part(server.Body().begin(), server.Body().begin() + 300000);
            std::fill(part.begin() + 200000, part.end(), std::uint8_t{});
            WriteBytes(fixture.Part(), part);
            auto job = fixture.Job(server.Body().size(), WideEtag);
            job.FlushedBytes = 200000;
            std::atomic_bool cancel{};
            FakeHost host;
            auto const before = server.Requests();
            auto const outcome = Run(job, cancel, host, options);
            Require(outcome.Finished.has_value() && fixture.PartMatches(server.Body()),
                "bytes past the flushed count were kept");
            auto const log = server.Log();
            Require(log.size() == before + 1 && log[before].Range == std::optional<std::uint64_t>{ 200000 },
                "the part did not resume from its flushed count");
        }
        {
            Fixture fixture{ server.Url() };
            WriteBytes(fixture.Part(), std::vector<std::uint8_t>(server.Body().begin(), server.Body().begin() + 300000));
            auto job = fixture.Job(server.Body().size(), WideEtag);
            job.FlushedBytes = 400000;
            std::atomic_bool cancel{};
            FakeHost host;
            auto const before = server.Requests();
            auto const outcome = Run(job, cancel, host, options);
            Require(outcome.Finished.has_value() && fixture.PartMatches(server.Body()),
                "a count past the part's length corrupted the resume");
            auto const log = server.Log();
            Require(log.size() == before + 1 && log[before].Range == std::optional<std::uint64_t>{ 300000 },
                "a count past the part's length was trusted over the length");
        }
    }

    // A download folder deep enough that a long temporary name for the table
    // cannot be created. A short one still can; where even that is too long,
    // the run stays on one connection instead of failing as a full disk.
    void ADeepFolderStillDownloads()
    {
        FileServer server{ FileServerOptions{ .Body = Content(2u * 1024u * 1024u), .Pace = std::chrono::milliseconds{ 15 } } };
        for (auto const partLength : { std::size_t{ 230 }, std::size_t{ 250 } })
        {
            TemporaryDirectory directory;
            auto const base = directory.Path().wstring();
            std::wstring const name = L"movie.mkv.part";
            auto const pad = partLength - base.size() - 2 - name.size();
            Require(pad > 0 && pad < 200, "the deep-folder test cannot build its path here");
            auto const root = directory.Path() / std::wstring(pad, L'd');
            std::filesystem::create_directories(root);
            SegmentedJob const job{ .Url = server.Url(), .Part = root / name, .Root = root };
            Require(job.Part.native().size() == partLength, "the deep-folder test built the wrong length");
            std::atomic_bool cancel{};
            FakeHost host;
            auto const outcome = Run(job, cancel, host, Quick());
            Require(outcome.Finished.has_value() && ReadBytes(job.Part) == server.Body(),
                "a download in a deep folder did not finish");
            Require(!std::filesystem::exists(Segments::TablePath(job.Part)), "the table outlived the download");
            if (partLength == 230)
            {
                Require(host.MostConnections() > 1, "a folder with room for the table's short name stayed on one connection");
            }
            else
            {
                Require(host.MostConnections() <= 1, "a run that cannot keep a table used several connections");
            }
        }
    }

    void APausedSegmentedDownloadResumesEverySegment()
    {
        FileServer server{ FileServerOptions{ .Body = Content(6u * 1024u * 1024u), .Pace = std::chrono::milliseconds{ 25 } } };
        Fixture fixture{ server.Url() };
        FakeHost host;
        std::atomic_bool cancel{};
        std::jthread pause{ [&cancel]()
        {
            std::this_thread::sleep_for(std::chrono::milliseconds{ 500 });
            cancel.store(true);
        } };
        auto const paused = Run(fixture.Job(), cancel, host, Quick());
        pause.join();
        Require(paused.Failure && paused.Failure->Kind == TransferFailureKind::Paused, "a paused transfer did not report a pause");
        auto const saved = Segments::Decode(ReadText(fixture.Table()));
        Require(saved.has_value(), "the paused table did not decode");
        Require(saved->Total == server.Body().size(), "the paused table has the wrong size");
        Require(saved->Segments.size() > 1, "the run never split");
        std::uint64_t done{};
        for (auto const& segment : saved->Segments) done += segment.Done;
        Require(done > 0 && done < server.Body().size(), "the paused table saved no partial progress");
        auto const before = server.Requests();

        auto const metadata = host.Metadata();
        Require(metadata.has_value(), "the response metadata was never reported");
        cancel.store(false);
        auto const resumed = Run(fixture.Job(metadata->second, metadata->first), cancel, host, Quick());
        Require(resumed.Finished.has_value() && fixture.PartMatches(server.Body()), "a resumed segmented download lost bytes");
        Require(!std::filesystem::exists(fixture.Table()), "the table outlived the resumed download");
        auto const log = server.Log();
        Require(log[before].Range.has_value(), "the resume did not ask for a range");
        Require(log[before].Status == 206, "the resume was not answered with a range");
    }

    void APartFromBeforeTablesResumesFromItsLength()
    {
        FileServer server{ FileServerOptions{ .Body = Content(1024u * 1024u) } };
        Fixture fixture{ server.Url() };
        WriteBytes(fixture.Part(), std::vector<std::uint8_t>(server.Body().begin(), server.Body().begin() + 300000));
        std::atomic_bool cancel{};
        FakeHost host;
        auto options = Quick();
        options.Tuning.MaxConnections = 1;
        auto const outcome = Run(fixture.Job(server.Body().size(), WideEtag), cancel, host, options);
        Require(outcome.Finished.has_value() && fixture.PartMatches(server.Body()), "a part from before tables did not resume");
        auto const log = server.Log();
        Require(log.size() == 1 && log[0].Range == std::optional<std::uint64_t>{ 300000 },
            "a part from before tables did not resume from its length");
    }

    void AResumeThroughARedirectToAnotherOriginContinues()
    {
        FileServer server{ FileServerOptions{ .Body = Content(1024u * 1024u) } };
        FileServer redirector{ FileServerOptions{ .RedirectTo = NarrowAscii(server.Url()) } };
        Fixture fixture{ redirector.Url(L"/resolve") };
        WriteBytes(fixture.Part(), std::vector<std::uint8_t>(server.Body().begin(), server.Body().begin() + 300000));
        std::atomic_bool cancel{};
        FakeHost host;
        auto options = Quick();
        options.Tuning.MaxConnections = 1;
        auto const outcome = Run(fixture.Job(server.Body().size(), WideEtag), cancel, host, options);
        Require(outcome.Finished.has_value() && fixture.PartMatches(server.Body()), "a resume through a redirect lost bytes");
        // One ranged request, confirmed by the validator: no second, whole-file one.
        auto const log = server.Log();
        Require(log.size() == 1 && log[0].Range == std::optional<std::uint64_t>{ 300000 },
            "a validated resume through another origin started over");
    }

    void AValidatorMismatchWhileResumingIsRefused()
    {
        FileServer server{ FileServerOptions{ .Body = Content(1024u * 1024u), .ETag = std::string{ "\"v2\"" }, .IgnoreIfRange = true } };
        Fixture fixture{ server.Url() };
        WriteBytes(fixture.Part(), std::vector<std::uint8_t>(server.Body().begin(), server.Body().begin() + 300000));
        std::atomic_bool cancel{};
        FakeHost host;
        auto const outcome = Run(fixture.Job(server.Body().size(), WideEtag), cancel, host, Quick());
        Require(Failed(outcome, TransferFailureKind::Permanent, DownloadFailureCode::InvalidRange),
            "a resume answered under another validator was appended");
    }

    void AnUnreadableTableRestartsFromZero()
    {
        FileServer server{ FileServerOptions{ .Body = Content(1024u * 1024u) } };
        Fixture fixture{ server.Url() };
        // A full-length part with holes: its length must not pass for progress.
        WriteBytes(fixture.Part(), std::vector<std::uint8_t>(server.Body().size(), 0));
        {
            std::ofstream table{ fixture.Table(), std::ios::binary };
            table << "{ torn";
        }
        std::atomic_bool cancel{};
        FakeHost host;
        auto const outcome = Run(fixture.Job(server.Body().size(), WideEtag), cancel, host, Quick());
        Require(outcome.Finished.has_value() && fixture.PartMatches(server.Body()), "an unreadable table did not restart cleanly");
        Require(!server.Log()[0].Range, "an unreadable table was trusted for a resume");
        Require(!std::filesystem::exists(fixture.Table()), "the unreadable table was kept");
    }

    void AStalledConnectionIsRetried()
    {
        FileServer server{ FileServerOptions{
            .Body = Content(3u * 1024u * 1024u), .Pace = std::chrono::milliseconds{ 2 }, .StallFirstRanged = true } };
        Fixture fixture{ server.Url() };
        std::atomic_bool cancel{};
        FakeHost host;
        auto options = Quick();
        options.Timeouts.Read = std::chrono::milliseconds{ 400 };
        auto const started = std::chrono::steady_clock::now();
        auto outcome = Run(fixture.Job(), cancel, host, options);
        // A stall with no other connection left receiving ends the run as a
        // retryable failure, as the reference's does; the engine then runs it
        // again from the saved table, which is what this does too.
        for (int attempt = 1; attempt < 3 && Failed(outcome, TransferFailureKind::Retryable, DownloadFailureCode::Network); ++attempt)
        {
            auto const metadata = host.Metadata();
            Require(metadata.has_value(), "the response metadata was never reported");
            outcome = Run(fixture.Job(metadata->second, metadata->first), cancel, host, options);
        }
        Require(std::chrono::steady_clock::now() - started < std::chrono::seconds{ 10 }, "a stalled connection was waited on");
        Require(outcome.Finished.has_value() && fixture.PartMatches(server.Body()), "a stalled connection lost the file");
        auto const log = server.Log();
        Require(std::count_if(log.begin(), log.end(), [](LoggedRequest const& entry) { return entry.Range.has_value(); }) >= 2,
            "the stalled segment was not asked for again");
    }

    void TheFirstResponseWaitEndsOnPauseOrTimeout()
    {
        FileServer server{ FileServerOptions{ .Body = Content(64u * 1024u), .ResponseDelay = std::chrono::seconds{ 5 } } };
        {
            Fixture fixture{ server.Url() };
            std::atomic_bool cancel{};
            FakeHost host;
            auto options = Quick();
            options.Timeouts.FirstResponse = std::chrono::milliseconds{ 300 };
            auto const started = std::chrono::steady_clock::now();
            auto const outcome = Run(fixture.Job(), cancel, host, options);
            Require(Failed(outcome, TransferFailureKind::Retryable, DownloadFailureCode::Network),
                "a source that never answered was not a retryable network failure");
            Require(std::chrono::steady_clock::now() - started < std::chrono::seconds{ 3 }, "the first response wait ignored its limit");
        }
        {
            Fixture fixture{ server.Url() };
            std::atomic_bool cancel{};
            FakeHost host;
            auto options = Quick();
            // The production poll interval: a pause is noticed within it.
            options.Timeouts.CancelPoll = std::chrono::milliseconds{ 250 };
            std::jthread pause{ [&cancel]()
            {
                std::this_thread::sleep_for(std::chrono::milliseconds{ 100 });
                cancel.store(true);
            } };
            auto const started = std::chrono::steady_clock::now();
            auto const outcome = Run(fixture.Job(), cancel, host, options);
            Require(outcome.Failure && outcome.Failure->Kind == TransferFailureKind::Paused,
                "a pause while waiting for the source was not a pause");
            Require(std::chrono::steady_clock::now() - started < std::chrono::milliseconds{ 1500 },
                "a pause while waiting for the source was not noticed quickly");
        }
    }

    void RetryAfterIsReadAsSeconds()
    {
        Downloads::Http::ResponseHead head;
        Require(!Downloads::Http::RetryAfterSeconds(head), "a missing Retry-After was read");
        head.RetryAfter = L"12";
        Require(Downloads::Http::RetryAfterSeconds(head) == std::optional<std::uint64_t>{ 12 }, "Retry-After seconds were not read");
        head.RetryAfter = L"Wed, 21 Oct 2026 07:28:00 GMT";
        Require(!Downloads::Http::RetryAfterSeconds(head), "a Retry-After date was read as seconds");
    }

    void AFileIsMissingOnlyWhenItsFolderIsThereWithoutIt()
    {
        TemporaryDirectory directory;
        auto const file = directory.Path() / L"movie.mkv";
        WriteBytes(file, std::vector<std::uint8_t>(5, 1));
        Require(Downloads::DetectFilePresence(directory.Path(), file)
                == Downloads::FilePresence{ .Kind = Downloads::FilePresenceKind::Present, .Bytes = 5 },
            "a present file was not found");
        Require(Downloads::DetectFilePresence(directory.Path(), directory.Path() / L"gone.mkv").Kind
                == Downloads::FilePresenceKind::Missing,
            "a missing file in a present folder was not missing");
        // An unplugged drive: neither the folder nor the file can be reached.
        auto const unplugged = directory.Path() / L"unplugged";
        Require(Downloads::DetectFilePresence(unplugged, unplugged / L"movie.mkv").Kind
                == Downloads::FilePresenceKind::FolderUnavailable,
            "an unreachable folder was taken for a missing file");
    }

    void RunSegmentedTransferTests()
    {
        SeveralConnectionsDeliverTheExactFile();
        ProgressReportsHowManyConnectionsAreReceiving();
        ASourceThatIgnoresRangesIsReadThroughOnOneConnection();
        AStandInClipIsNotTakenForTheVideo();
        TheFirstConnectionWaitsAtTheSplitForTheProbesAnswer();
        ARefusedProbeKeepsTheRunOnOneConnection();
        ARefusalAfterRangesAreProvenSetsTheCeiling();
        A403ToAnExtraConnectionIsALimitNotAnExpiredLink();
        DifferentBytesFromTwoConnectionsFailAndLeaveNothing();
        ASourceWithoutAValidatorStaysOnOneConnection();
        AnyUnmetConditionKeepsTheRunOnOneConnection();
        ASizeHintIsNeverTheLengthOfAnUnstatedBody();
        APartIsTrustedOnlyUpToItsFlushedCount();
        ADeepFolderStillDownloads();
        APausedSegmentedDownloadResumesEverySegment();
        APartFromBeforeTablesResumesFromItsLength();
        AResumeThroughARedirectToAnotherOriginContinues();
        AValidatorMismatchWhileResumingIsRefused();
        AnUnreadableTableRestartsFromZero();
        AStalledConnectionIsRetried();
        TheFirstResponseWaitEndsOnPauseOrTimeout();
        RetryAfterIsReadAsSeconds();
        AFileIsMissingOnlyWhenItsFolderIsThereWithoutIt();
    }

    // ---- The engine's own rules around a transfer ----

    [[nodiscard]] TransferEngineOptions QuickEngine()
    {
        TransferEngineOptions options;
        options.Transfer = Quick();
        return options;
    }

    [[nodiscard]] std::optional<DownloadRecord> FindJob(TransferEngine& engine, std::wstring const& jobId)
    {
        auto const records = engine.List();
        auto const found = std::find_if(records.begin(), records.end(), [&jobId](DownloadRecord const& record)
        {
            return record.JobId == jobId;
        });
        return found == records.end() ? std::nullopt : std::optional<DownloadRecord>{ *found };
    }

    void APauseWhileWaitingForTheSourceIsNoticed()
    {
        FileServer server{ FileServerOptions{ .Body = Content(64u * 1024u), .ResponseDelay = std::chrono::seconds{ 8 } } };
        TemporaryDirectory temporary;
        auto options = QuickEngine();
        options.Transfer.Timeouts.CancelPoll = std::chrono::milliseconds{ 250 };
        TransferEngine engine{ temporary.Path(), {}, options };
        engine.SetAccount(L"https://account.invalid", L"user-a");
        auto const started = engine.Start(MakeRequest(server.Url(), L"waiting.mkv", 64u * 1024u, false, std::nullopt, L"movie:waiting"));
        static_cast<void>(WaitForStatus(engine, started.JobId, DownloadStatus::Downloading));
        // Long enough for the worker to be waiting on the request.
        std::this_thread::sleep_for(std::chrono::milliseconds{ 300 });
        auto const before = std::chrono::steady_clock::now();
        engine.Pause(started.JobId);
        Require(std::chrono::steady_clock::now() - before < std::chrono::seconds{ 2 },
            "a pause waited for a source that had not answered");
        auto const paused = FindJob(engine, started.JobId);
        Require(paused && paused->Status == DownloadStatus::Paused, "a pause while waiting did not pause");
        Require(server.Requests() == 0, "the paused request was answered");
    }

    void ASourceThatNeverAnswersIsRetriedThenFails()
    {
        FileServer server{ FileServerOptions{ .Body = Content(64u * 1024u), .ResponseDelay = std::chrono::seconds{ 8 } } };
        TemporaryDirectory temporary;
        auto options = QuickEngine();
        options.Transfer.Timeouts.FirstResponse = std::chrono::milliseconds{ 200 };
        TransferEngine engine{ temporary.Path(), {}, options };
        engine.SetAccount(L"https://account.invalid", L"user-a");
        auto const started = engine.Start(MakeRequest(server.Url(), L"silent.mkv", 64u * 1024u, false, std::nullopt, L"movie:silent"));
        auto const failed = WaitForStatus(engine, started.JobId, DownloadStatus::Failed);
        Require(failed.Failure == DownloadFailureCode::Network, "a source that never answered was not a network failure");
        Require(server.Requests() == 0, "a request the engine gave up on was answered");
    }

    void ASlowSubtitleDoesNotHoldTheDownload()
    {
        FileServer video{ FileServerOptions{ .Body = Content(128u * 1024u) } };
        FileServer subtitle{ FileServerOptions{ .Body = Content(100), .ResponseDelay = std::chrono::seconds{ 8 } } };
        TemporaryDirectory temporary;
        auto options = QuickEngine();
        options.SubtitleResponse = std::chrono::milliseconds{ 300 };
        TransferEngine engine{ temporary.Path(), {}, options };
        engine.SetAccount(L"https://account.invalid", L"user-a");
        auto const before = std::chrono::steady_clock::now();
        auto const started = engine.Start(MakeRequest(
            video.Url(),
            L"slow-subtitle.mkv",
            video.Body().size(),
            false,
            SubtitleRequest{ .Url = subtitle.Url(L"/subtitle.srt"), .Language = L"eng", .Id = L"subtitle:slow" },
            L"movie:slow-subtitle"));
        auto const done = WaitForStatus(engine, started.JobId, DownloadStatus::Done);
        Require(std::chrono::steady_clock::now() - before < std::chrono::seconds{ 4 }, "a slow subtitle held the download");
        Require(!done.SubtitleFileName, "a subtitle that never answered was recorded");
        auto const files = engine.FilesForPlayback(done.JobId);
        Require(ReadBytes(files.VideoPath) == video.Body(), "the video behind a slow subtitle was corrupted");
        Require(!files.SubtitlePath, "a subtitle that never answered was saved");
    }

    // Removed while it waited for its subtitle, after the video itself was
    // complete: the copy it was replacing stays, and its own files go.
    void ARemovalWhileFinishingKeepsTheReplacedCopy()
    {
        FileServer first{ FileServerOptions{ .Body = Content(256u * 1024u) } };
        auto secondBody = Content(300u * 1024u);
        std::reverse(secondBody.begin(), secondBody.end());
        FileServer second{ FileServerOptions{ .Body = std::move(secondBody) } };
        FileServer subtitle{ FileServerOptions{ .Body = Content(100), .ResponseDelay = std::chrono::seconds{ 8 } } };
        TemporaryDirectory temporary;
        TransferEngine engine{ temporary.Path(), {}, QuickEngine() };
        engine.SetAccount(L"https://account.invalid", L"user-a");
        auto const original = engine.Start(MakeRequest(first.Url(), L"kept.mkv", first.Body().size(), false, std::nullopt, L"movie:finishing"));
        auto const originalDone = WaitForStatus(engine, original.JobId, DownloadStatus::Done);
        auto const replacement = engine.Start(MakeRequest(
            second.Url(),
            L"replacement.mkv",
            second.Body().size(),
            true,
            SubtitleRequest{ .Url = subtitle.Url(L"/subtitle.srt"), .Language = L"eng", .Id = L"subtitle:finishing" },
            L"movie:finishing"));
        // The video is in place once its part is renamed; the subtitle is next.
        WaitUntil([&replacement]() { return std::filesystem::exists(replacement.TargetPath()); },
            "the replacement video never completed");
        engine.Remove(replacement.JobId);
        WaitUntil([&engine, &replacement]()
        {
            return !ListsJob(engine, replacement.JobId) && !std::filesystem::exists(replacement.TargetPath());
        }, "a removed replacement left its video behind");
        auto const kept = FindJob(engine, originalDone.JobId);
        Require(kept && kept->Status == DownloadStatus::Done, "a removal while finishing superseded the replaced copy");
        Require(ReadBytes(engine.FilesForPlayback(originalDone.JobId).VideoPath) == first.Body(),
            "the replaced copy's file was touched");
    }

    // A replacement from the same source shares its file names with the copy
    // it replaces. Removing it while it runs must not delete what that copy
    // still names.
    void ARemovedRunNeverDeletesFilesAnotherRecordNames()
    {
        FileServer server{ FileServerOptions{ .Body = Content(4u * 1024u * 1024u), .Pace = std::chrono::milliseconds{ 40 } } };
        TemporaryDirectory temporary;
        TransferEngine engine{ temporary.Path(), {}, QuickEngine() };
        engine.SetAccount(L"https://account.invalid", L"user-a");
        server.ForceStatus(401);
        auto const expired = engine.Start(MakeRequest(server.Url(), L"shared.mkv", server.Body().size(), false, std::nullopt, L"movie:shared"));
        static_cast<void>(WaitForStatus(engine, expired.JobId, DownloadStatus::Failed));
        server.ForceStatus(0);
        auto const replacement = engine.Start(MakeRequest(server.Url(), L"shared.mkv", server.Body().size(), true, std::nullopt, L"movie:shared"));
        Require(replacement.PartialPath() == expired.PartialPath(), "the shared-name scenario did not share names");
        static_cast<void>(WaitForStatus(engine, replacement.JobId, DownloadStatus::Downloading));
        WaitForFile(replacement.PartialPath());
        engine.Remove(replacement.JobId);
        WaitUntil([&engine, &expired, &replacement]()
        {
            return !ListsJob(engine, replacement.JobId) && ListsJob(engine, expired.JobId);
        }, "the replaced copy did not come back after its replacement was removed");
        // The run forgets the record only after its cleanup, so once the index no
        // longer names it, the cleanup has happened.
        auto const index = temporary.Path() / L"downloads-index.json";
        auto const removedId = NarrowAscii(replacement.JobId);
        WaitUntil([&index, &removedId]() { return ReadText(index).find(removedId) == std::string::npos; },
            "a removed run never finished its cleanup");
        Require(std::filesystem::exists(expired.PartialPath()), "a removed run deleted a file another record names");
    }

    void ActiveConnectionsReachTheRecords()
    {
        FileServer server{ FileServerOptions{ .Body = Content(4u * 1024u * 1024u), .Pace = std::chrono::milliseconds{ 15 } } };
        TemporaryDirectory temporary;
        TransferEngine engine{ temporary.Path(), {}, QuickEngine() };
        engine.SetAccount(L"https://account.invalid", L"user-a");
        std::atomic_uint32_t most{};
        std::atomic_bool outsideDownloading{};
        auto const token = engine.AddChangedHandler([&most, &outsideDownloading](DownloadRecord const& record)
        {
            if (record.Status != DownloadStatus::Downloading && record.ActiveConnections != 0)
            {
                outsideDownloading.store(true);
            }
            auto observed = most.load();
            while (record.ActiveConnections > observed && !most.compare_exchange_weak(observed, record.ActiveConnections))
            {
            }
        });
        auto const started = engine.Start(MakeRequest(server.Url(), L"connections.mkv", server.Body().size(), false, std::nullopt, L"movie:connections"));
        auto const done = WaitForStatus(engine, started.JobId, DownloadStatus::Done);
        engine.RemoveChangedHandler(token);
        Require(most.load() > 1, "the records never showed more than one connection");
        Require(!outsideDownloading.load(), "a record that was not downloading showed connections");
        Require(done.ActiveConnections == 0, "a finished record still shows connections");
        Require(ReadBytes(engine.FilesForPlayback(done.JobId).VideoPath) == server.Body(), "a multi-connection engine download was corrupted");
        Require(ReadText(temporary.Path() / L"downloads-index.json").find("ctiveConnections") == std::string::npos,
            "live connection counts were stored in the index");
    }

    [[nodiscard]] DownloadRecord SeededRecord(
        std::wstring jobId,
        std::wstring videoId,
        std::filesystem::path root,
        std::wstring fileName,
        DownloadStatus status)
    {
        return DownloadRecord{
            .JobId = std::move(jobId),
            .AccountKey = MakeAccountKey(L"https://account.invalid", L"user-a"),
            .Media = { .VideoId = videoId, .ItemId = videoId, .MediaType = L"movie", .Title = L"Seeded" },
            .FileName = std::move(fileName),
            .RootPath = std::move(root),
            .Status = status,
            .TotalBytes = 5,
            .DownloadedBytes = 5,
            .SourceFingerprint = Sha256Hex(L"https://source.invalid/" + videoId),
            .CreatedAt = 1,
            .UpdatedAt = 1,
        };
    }

    void MissingFilesAreReconciledInThreeStates()
    {
        TemporaryDirectory temporary;
        auto const folder = temporary.Path() / L"downloads";
        std::filesystem::create_directories(folder);
        auto unplugged = SeededRecord(L"00000000000000000000000000000001", L"movie:unplugged", temporary.Path() / L"unplugged", L"unplugged.mkv", DownloadStatus::Done);
        auto missing = SeededRecord(L"00000000000000000000000000000002", L"movie:missing", folder, L"missing.mkv", DownloadStatus::Done);
        auto returned = SeededRecord(L"00000000000000000000000000000003", L"movie:returned", folder, L"returned.mkv", DownloadStatus::Failed);
        returned.Failure = DownloadFailureCode::MissingFile;
        auto shorter = SeededRecord(L"00000000000000000000000000000004", L"movie:shorter", folder, L"shorter.mkv", DownloadStatus::Failed);
        shorter.Failure = DownloadFailureCode::MissingFile;
        WriteBytes(returned.TargetPath(), std::vector<std::uint8_t>(5, 1));
        WriteBytes(shorter.TargetPath(), std::vector<std::uint8_t>(4, 1));
        {
            DownloadIndexStore seed{ temporary.Path() };
            seed.Apply({ unplugged, missing, returned, shorter });
        }
        TransferEngine engine{ temporary.Path(), {}, QuickEngine() };
        engine.SetAccount(L"https://account.invalid", L"user-a");
        auto const away = FindJob(engine, unplugged.JobId);
        Require(away && away->Status == DownloadStatus::Done, "an unreachable folder marked its download missing");
        auto const gone = FindJob(engine, missing.JobId);
        Require(gone && gone->Status == DownloadStatus::Failed && gone->Failure == DownloadFailureCode::MissingFile,
            "a file missing from its folder was not marked missing");
        auto const back = FindJob(engine, returned.JobId);
        Require(back && back->Status == DownloadStatus::Done && !back->Failure, "a file that came back stayed marked missing");
        auto const wrong = FindJob(engine, shorter.JobId);
        Require(wrong && wrong->Status == DownloadStatus::Failed, "a file of the wrong size was taken back");
    }

    void ALockedIndexIsRetriedBeforeFailing()
    {
        TemporaryDirectory temporary;
        DownloadIndexStore store{ temporary.Path() };
        auto const record = SeededRecord(L"00000000000000000000000000000005", L"movie:locked", temporary.Path() / L"downloads", L"locked.mkv", DownloadStatus::Done);
        store.Apply({ record });
        auto const index = temporary.Path() / L"downloads-index.json";
        auto const holdExclusively = [&index](std::chrono::milliseconds duration)
        {
            HANDLE const file = CreateFileW(index.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE)
            {
                throw std::runtime_error{ "The test could not lock the download index." };
            }
            return std::jthread{ [file, duration]()
            {
                std::this_thread::sleep_for(duration);
                CloseHandle(file);
            } };
        };
        {
            auto releaser = holdExclusively(std::chrono::milliseconds{ 150 });
            auto const loaded = store.Load(false);
            Require(loaded.size() == 1, "a briefly locked index was not read after a retry");
        }
        {
            auto releaser = holdExclusively(std::chrono::milliseconds{ 1500 });
            bool failed{};
            try
            {
                static_cast<void>(store.Load(false));
            }
            catch (...)
            {
                failed = true;
            }
            Require(failed, "an index locked past every retry was read");
        }
        Require(store.Load(false).size() == 1, "a locked index was treated as corrupt");
        for (auto const& entry : std::filesystem::directory_iterator{ temporary.Path() })
        {
            Require(entry.path().filename().wstring().find(L".corrupt-") == std::wstring::npos,
                "a locked index was set aside as corrupt");
        }
    }

    void AnIndexIsReportedOnlyWhenItWasSetAside()
    {
        {
            TemporaryDirectory temporary;
            int reports{};
            TransferEngine engine{ temporary.Path(), [&reports](std::wstring_view) { ++reports; }, QuickEngine() };
            engine.SetAccount(L"https://account.invalid", L"user-a");
            Require(engine.List().empty() && reports == 0, "a missing index was reported or produced downloads");
        }
        {
            TemporaryDirectory temporary;
            // What a power cut after an unflushed write leaves: the right size, all zeros.
            WriteBytes(temporary.Path() / L"downloads-index.json", std::vector<std::uint8_t>(512, 0));
            int reports{};
            TransferEngine engine{ temporary.Path(), [&reports](std::wstring_view) { ++reports; }, QuickEngine() };
            Require(reports == 1, "a torn index was not reported to the crash log exactly once");
            auto kept = 0;
            for (auto const& entry : std::filesystem::directory_iterator{ temporary.Path() })
            {
                if (entry.path().filename().wstring().starts_with(L"downloads-index.corrupt-")
                    && ReadBytes(entry.path()) == std::vector<std::uint8_t>(512, 0))
                {
                    ++kept;
                }
            }
            Require(kept == 1, "a torn index was not kept aside unchanged");
        }
    }

    // An index and part written by the previous build, with no sidecar, still
    // resume from the part's length.
    void APreviousBuildsPartialDownloadResumes()
    {
        FileServer server{ FileServerOptions{ .Body = Content(1024u * 1024u) } };
        TemporaryDirectory temporary;
        auto record = SeededRecord(L"0123456789abcdef0123456789abcde0", L"movie:legacy", temporary.Path() / L"downloads", L"legacy.mkv", DownloadStatus::Paused);
        auto request = MakeRequest(server.Url(), L"legacy.mkv", server.Body().size(), false, std::nullopt, L"movie:legacy");
        record.SourceFingerprint = Sha256Hex(request.Request.Url);
        record.TotalBytes = server.Body().size();
        record.DownloadedBytes = 300000;
        record.Validator = WideEtag;
        record.ExplicitPause = true;
        std::filesystem::create_directories(record.RootPath);
        WriteBytes(record.PartialPath(), std::vector<std::uint8_t>(server.Body().begin(), server.Body().begin() + 300000));
        {
            DownloadIndexStore seed{ temporary.Path() };
            seed.Apply({ record });
            RequestVault vault{ temporary.Path() / L"download-requests" };
            vault.Write(record.JobId, std::move(request.Request));
        }
        TransferEngine engine{ temporary.Path(), {}, QuickEngine() };
        engine.SetAccount(L"https://account.invalid", L"user-a");
        engine.Resume(record.JobId);
        auto const done = WaitForStatus(engine, record.JobId, DownloadStatus::Done);
        Require(ReadBytes(engine.FilesForPlayback(done.JobId).VideoPath) == server.Body(), "a previous build's partial download was corrupted");
        auto const log = server.Log();
        Require(!log.empty() && log[0].Range == std::optional<std::uint64_t>{ 300000 },
            "a previous build's partial download did not resume from its length");
    }

    // Holds the download index open the way another program might, until
    // released. Sharing reads only: the index can be read but not replaced.
    // Sharing nothing: it cannot be read either.
    class IndexHold final
    {
    public:
        IndexHold(std::filesystem::path const& index, DWORD share)
            : m_file(CreateFileW(index.c_str(), GENERIC_READ, share, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr))
        {
            if (!m_file)
            {
                throw std::runtime_error{ "The test could not hold the download index." };
            }
        }

        void Release() noexcept
        {
            m_file.close();
        }

    private:
        winrt::file_handle m_file;
    };

    [[nodiscard]] std::filesystem::path IndexOf(TemporaryDirectory const& temporary)
    {
        return temporary.Path() / L"downloads-index.json";
    }

    // The worker reads the index after taking a job's lease. A read that fails
    // (held open by another program) puts the job back instead of ending the
    // process, and it runs once the index can be read.
    void AnUnreadableIndexPutsTheJobBack()
    {
        FileServer server{ FileServerOptions{ .Body = Content(256u * 1024u) } };
        TemporaryDirectory temporary;
        auto record = SeededRecord(L"0123456789abcdef0123456789abcd01", L"movie:index-held", temporary.Path() / L"downloads", L"index-held.mkv", DownloadStatus::Queued);
        auto request = MakeRequest(server.Url(), L"index-held.mkv", server.Body().size(), false, std::nullopt, L"movie:index-held");
        record.SourceFingerprint = Sha256Hex(request.Request.Url);
        record.TotalBytes = 0;
        record.DownloadedBytes = 0;
        std::filesystem::create_directories(record.RootPath);
        DownloadIndexStore seed{ temporary.Path() };
        seed.Apply({ record });
        {
            RequestVault vault{ temporary.Path() / L"download-requests" };
            vault.Write(record.JobId, std::move(request.Request));
        }
        // The job's own lease keeps the worker away until the index is held.
        auto lease = std::make_unique<HaloDesktop::Storage::FileMutationLock>(
            seed.Paths().DataRoot / (L"download-job-" + record.JobId));
        TransferEngine engine{ temporary.Path(), {}, QuickEngine() };
        engine.SetAccount(L"https://account.invalid", L"user-a");
        IndexHold hold{ IndexOf(temporary), 0 };
        lease.reset();
        // Longer than every read retry, so at least one worker read fails.
        std::this_thread::sleep_for(std::chrono::milliseconds{ 1500 });
        Require(server.Requests() == 0, "a job ran although its index could not be read");
        hold.Release();
        auto const done = WaitForStatus(engine, record.JobId, DownloadStatus::Done);
        Require(ReadBytes(engine.FilesForPlayback(done.JobId).VideoPath) == server.Body(),
            "a job put back after an unreadable index was corrupted");
    }

    // An index that cannot be written while a download runs and ends: the
    // progress stores are skipped, the finished download is shown as done,
    // and the next launch finishes it from the file in place without asking
    // the source again.
    void AnIndexThatCannotBeWrittenDoesNotFailADownload()
    {
        FileServer server{ FileServerOptions{ .Body = Content(2u * 1024u * 1024u), .Pace = std::chrono::milliseconds{ 10 } } };
        TemporaryDirectory temporary;
        auto options = QuickEngine();
        options.Transfer.Tuning.MaxConnections = 1;
        options.Transfer.Timeouts.Durable = std::chrono::milliseconds{ 50 };
        DownloadRecord done;
        std::size_t requests{};
        {
            TransferEngine engine{ temporary.Path(), {}, options };
            engine.SetAccount(L"https://account.invalid", L"user-a");
            auto const started = engine.Start(MakeRequest(server.Url(), L"unwritable.mkv", server.Body().size(), false, std::nullopt, L"movie:unwritable"));
            // The part has data only after the start and the response were stored.
            WaitForFile(started.PartialPath());
            IndexHold hold{ IndexOf(temporary), FILE_SHARE_READ };
            done = WaitForStatus(engine, started.JobId, DownloadStatus::Done);
            Require(ReadBytes(engine.FilesForPlayback(done.JobId).VideoPath) == server.Body(),
                "a download that could not store its progress was corrupted");
            Require(ReadText(IndexOf(temporary)).find("\"downloading\"") != std::string::npos,
                "the test did not keep the end of the download from being stored");
            hold.Release();
            requests = server.Requests();
        }
        TransferEngine relaunched{ temporary.Path(), {}, options };
        relaunched.SetAccount(L"https://account.invalid", L"user-a");
        auto const finished = WaitForStatus(relaunched, done.JobId, DownloadStatus::Done);
        Require(server.Requests() == requests, "a finished file was downloaded again at the next launch");
        Require(ReadBytes(relaunched.FilesForPlayback(finished.JobId).VideoPath) == server.Body(),
            "a finished file was changed at the next launch");
        Require(ReadText(IndexOf(temporary)).find("\"done\"") != std::string::npos,
            "the next launch did not store the finished download");
    }

    // One source under two videos would give both records one file name, and
    // removing one would delete the other's video.
    void TwoDownloadsNeverShareAFile()
    {
        FileServer server{ FileServerOptions{ .Body = Content(1024u * 1024u), .Pace = std::chrono::milliseconds{ 20 } } };
        TemporaryDirectory temporary;
        TransferEngine engine{ temporary.Path(), {}, QuickEngine() };
        engine.SetAccount(L"https://account.invalid", L"user-a");
        auto const first = engine.Start(MakeRequest(server.Url(), L"same-name.mkv", server.Body().size(), false, std::nullopt, L"movie:name-one"));
        bool refused{};
        try
        {
            static_cast<void>(engine.Start(MakeRequest(server.Url(), L"same-name.mkv", server.Body().size(), false, std::nullopt, L"movie:name-two")));
        }
        catch (std::runtime_error const&)
        {
            refused = true;
        }
        Require(refused, "a second download was given a file another download names");
        Require(engine.List().size() == 1, "a refused download was listed");
        static_cast<void>(WaitForStatus(engine, first.JobId, DownloadStatus::Done));

        // Two such records already in the index (written before this check):
        // removing the unfinished one keeps the file the finished one names.
        auto const folder = temporary.Path() / L"downloads";
        auto kept = SeededRecord(L"00000000000000000000000000000011", L"movie:shared-kept", folder, L"shared-seed.mkv", DownloadStatus::Done);
        auto removed = SeededRecord(L"00000000000000000000000000000012", L"movie:shared-removed", folder, L"shared-seed.mkv", DownloadStatus::Paused);
        removed.ExplicitPause = true;
        WriteBytes(kept.TargetPath(), std::vector<std::uint8_t>(5, 7));
        DownloadIndexStore seed{ temporary.Path() };
        seed.Apply({ kept, removed });
        engine.SetAccount(L"https://account.invalid", L"user-a");
        engine.Remove(removed.JobId);
        Require(!ListsJob(engine, removed.JobId), "the removed record was still listed");
        Require(std::filesystem::exists(kept.TargetPath()), "removing one record deleted the file another names");
        auto const stillDone = FindJob(engine, kept.JobId);
        Require(stillDone && stillDone->Status == DownloadStatus::Done, "the record that kept its file was not left done");
    }

    // Cancel is for a download that has not finished. One that finished first
    // keeps its file; Delete of a ready download still removes it.
    void ACancelLeavesAFinishedDownload()
    {
        FileServer server{ FileServerOptions{ .Body = Content(256u * 1024u) } };
        TemporaryDirectory temporary;
        TransferEngine engine{ temporary.Path(), {}, QuickEngine() };
        engine.SetAccount(L"https://account.invalid", L"user-a");
        auto const started = engine.Start(MakeRequest(server.Url(), L"cancel-done.mkv", server.Body().size(), false, std::nullopt, L"movie:cancel-done"));
        auto const done = WaitForStatus(engine, started.JobId, DownloadStatus::Done);
        Require(!engine.Cancel(done.JobId), "a cancel removed a finished download");
        Require(ReadBytes(engine.FilesForPlayback(done.JobId).VideoPath) == server.Body(), "a cancel touched a finished download's file");
        engine.Remove(done.JobId);
        Require(!ListsJob(engine, done.JobId) && !std::filesystem::exists(done.TargetPath()),
            "deleting a ready download did not remove it");

        FileServer slow{ FileServerOptions{ .Body = Content(4u * 1024u * 1024u), .Pace = std::chrono::milliseconds{ 40 } } };
        auto const running = engine.Start(MakeRequest(slow.Url(), L"cancel-running.mkv", slow.Body().size(), false, std::nullopt, L"movie:cancel-running"));
        WaitForFile(running.PartialPath());
        Require(engine.Cancel(running.JobId), "a running download was not cancelled");
        WaitUntil([&engine, &running]()
        {
            return !ListsJob(engine, running.JobId)
                && !std::filesystem::exists(running.PartialPath())
                && !std::filesystem::exists(TablePathOf(running));
        }, "a cancelled download left its partial file");
    }

    [[nodiscard]] DownloadRecord SeedQueuedJob(
        TemporaryDirectory const& temporary,
        FileServer const& server,
        std::wstring jobId,
        std::wstring const& videoId,
        std::wstring const& fileName)
    {
        auto record = SeededRecord(std::move(jobId), videoId, temporary.Path() / L"downloads", fileName, DownloadStatus::Queued);
        auto request = MakeRequest(server.Url(), fileName, server.Body().size(), false, std::nullopt, videoId);
        record.SourceFingerprint = Sha256Hex(request.Request.Url);
        record.TotalBytes = 0;
        record.DownloadedBytes = 0;
        std::filesystem::create_directories(record.RootPath);
        DownloadIndexStore seed{ temporary.Path() };
        seed.Apply({ record });
        RequestVault vault{ temporary.Path() / L"download-requests" };
        vault.Write(record.JobId, std::move(request.Request));
        return record;
    }

    // A pause that comes after the worker took the job's lease but before it
    // started the transfer: the pause is recorded first and the worker keeps
    // it, so the call neither waits out a whole download nor loses the pause.
    void APauseBeforeTheTransferStartsHolds()
    {
        FileServer server{ FileServerOptions{ .Body = Content(1024u * 1024u), .Pace = std::chrono::milliseconds{ 10 } } };
        TemporaryDirectory temporary;
        auto const record = SeedQueuedJob(temporary, server, L"0123456789abcdef0123456789abcd02", L"movie:pause-window", L"pause-window.mkv");
        auto lease = std::make_unique<HaloDesktop::Storage::FileMutationLock>(
            DownloadIndexStore{ temporary.Path() }.Paths().DataRoot / (L"download-job-" + record.JobId));
        TransferEngine engine{ temporary.Path(), {}, QuickEngine() };
        engine.SetAccount(L"https://account.invalid", L"user-a");
        // The index held unreadable for a moment keeps the worker between
        // taking the lease and starting the transfer.
        IndexHold hold{ IndexOf(temporary), 0 };
        lease.reset();
        std::this_thread::sleep_for(std::chrono::milliseconds{ 300 });
        std::exception_ptr pauseFailure;
        std::jthread pause{ [&engine, &record, &pauseFailure]()
        {
            try { engine.Pause(record.JobId); }
            catch (...) { pauseFailure = std::current_exception(); }
        } };
        std::this_thread::sleep_for(std::chrono::milliseconds{ 100 });
        hold.Release();
        pause.join();
        Require(!pauseFailure, "a pause before the transfer started failed");
        std::this_thread::sleep_for(std::chrono::milliseconds{ 300 });
        auto const paused = FindJob(engine, record.JobId);
        Require(paused && paused->Status == DownloadStatus::Paused, "a pause before the transfer started was lost");
        Require(server.Requests() == 0, "a download paused before it started was transferred");
    }

    // A pause or exit while the subtitle is fetched does not finish the
    // download without it: it is fetched by the next run, from the file in
    // place, without downloading the video again.
    void APauseDuringTheSubtitleKeepsItForLater()
    {
        FileServer video{ FileServerOptions{ .Body = Content(128u * 1024u) } };
        FileServer subtitle{ FileServerOptions{ .Body = Content(100), .ResponseDelay = std::chrono::seconds{ 8 }, .DelayFirstOnly = true } };
        TemporaryDirectory temporary;
        TransferEngine engine{ temporary.Path(), {}, QuickEngine() };
        engine.SetAccount(L"https://account.invalid", L"user-a");
        auto const started = engine.Start(MakeRequest(
            video.Url(),
            L"subtitle-later.mkv",
            video.Body().size(),
            false,
            SubtitleRequest{ .Url = subtitle.Url(L"/subtitle.srt"), .Language = L"eng", .Id = L"subtitle:later" },
            L"movie:subtitle-later"));
        WaitUntil([&started]() { return std::filesystem::exists(started.TargetPath()); },
            "the video never completed");
        engine.Pause(started.JobId);
        auto const paused = FindJob(engine, started.JobId);
        Require(paused && paused->Status == DownloadStatus::Paused, "a pause during the subtitle finished the download");
        Require(!paused->SubtitleFileName, "a subtitle that was stopped was recorded");
        Require(ReadBytes(started.TargetPath()) == video.Body(), "a pause during the subtitle touched the video");
        auto const requests = video.Requests();
        engine.Resume(started.JobId);
        auto const done = WaitForStatus(engine, started.JobId, DownloadStatus::Done);
        Require(done.SubtitleFileName.has_value(), "the stopped subtitle was not fetched on the next run");
        auto const files = engine.FilesForPlayback(done.JobId);
        Require(files.SubtitlePath.has_value(), "the stopped subtitle was not saved on the next run");
        Require(ReadBytes(files.VideoPath) == video.Body(), "the next run changed the video");
        Require(video.Requests() == requests, "the next run downloaded the video again");
    }

    // Signing in after using Halo without an account: the sign-in paused the
    // previous owner's running download, and the adoption carries it on. A
    // download the viewer paused stays paused.
    void AnAdoptedDownloadCarriesOnUnlessTheViewerPausedIt()
    {
        FileServer server{ FileServerOptions{ .Body = Content(2u * 1024u * 1024u), .Pace = std::chrono::milliseconds{ 20 } } };
        TemporaryDirectory temporary;
        TransferEngine engine{ temporary.Path(), {}, QuickEngine() };
        engine.SetAccount(L"https://device.invalid", L"guest");
        auto const running = engine.Start(MakeRequest(server.Url(), L"carried.mkv", server.Body().size(), false, std::nullopt, L"movie:carried"));
        auto const held = engine.Start(MakeRequest(server.Url(L"/held.mkv"), L"held.mkv", server.Body().size(), false, std::nullopt, L"movie:held"));
        engine.Pause(held.JobId);
        WaitForFile(running.PartialPath());
        engine.SetAccount(L"https://account.invalid", L"user-a");
        engine.AdoptAccount(
            MakeAccountKey(L"https://device.invalid", L"guest"),
            MakeAccountKey(L"https://account.invalid", L"user-a"));
        auto const done = WaitForStatus(engine, running.JobId, DownloadStatus::Done);
        Require(ReadBytes(engine.FilesForPlayback(done.JobId).VideoPath) == server.Body(), "an adopted download was corrupted");
        auto const stillHeld = FindJob(engine, held.JobId);
        Require(stillHeld && stillHeld->Status == DownloadStatus::Paused, "a download the viewer paused was resumed by the adoption");
    }

    // A replacement that is being removed never finishes, so the copy it
    // named can be removed (as an adoption discarding both must) instead of
    // being kept forever and coming back as a second copy.
    void ACopyWhoseReplacementIsBeingRemovedCanBeRemoved()
    {
        TemporaryDirectory temporary;
        DownloadIndexStore seed{ temporary.Path() };
        auto const folder = seed.Paths().DefaultDownloadDirectory;
        std::filesystem::create_directories(folder);
        auto backup = SeededRecord(L"00000000000000000000000000000021", L"movie:replaced", folder, L"backup.mkv", DownloadStatus::Done);
        auto replacer = SeededRecord(L"00000000000000000000000000000022", L"movie:replaced", folder, L"replacer.mkv", DownloadStatus::Paused);
        replacer.Replacement = Downloads::ReplacementBackup{ .JobId = backup.JobId, .RootPath = folder, .FileName = backup.FileName };
        replacer.PendingDeletion = true;
        replacer.ExplicitPause = true;
        WriteBytes(backup.TargetPath(), std::vector<std::uint8_t>(5, 3));
        WriteBytes(replacer.PartialPath(), std::vector<std::uint8_t>(3, 4));
        seed.Apply({ backup, replacer });
        // Held open, so the launch cannot finish the replacement's removal.
        winrt::file_handle const part{ CreateFileW(
            replacer.PartialPath().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr) };
        Require(static_cast<bool>(part), "the test could not hold the replacement's part");
        TransferEngine engine{ temporary.Path(), {}, QuickEngine() };
        engine.SetAccount(L"https://account.invalid", L"user-a");
        Require(ListsJob(engine, backup.JobId), "the copy of a removed replacement was not shown");
        engine.Remove(backup.JobId);
        Require(!ListsJob(engine, backup.JobId), "the copy of a removed replacement could not be removed");
        Require(!std::filesystem::exists(backup.TargetPath()), "the removed copy's file was kept");
    }

    void RunDownloadEngineSafetyTests()
    {
        APauseWhileWaitingForTheSourceIsNoticed();
        ASourceThatNeverAnswersIsRetriedThenFails();
        ASlowSubtitleDoesNotHoldTheDownload();
        ARemovalWhileFinishingKeepsTheReplacedCopy();
        ARemovedRunNeverDeletesFilesAnotherRecordNames();
        ActiveConnectionsReachTheRecords();
        MissingFilesAreReconciledInThreeStates();
        ALockedIndexIsRetriedBeforeFailing();
        AnIndexIsReportedOnlyWhenItWasSetAside();
        APreviousBuildsPartialDownloadResumes();
        AnUnreadableIndexPutsTheJobBack();
        AnIndexThatCannotBeWrittenDoesNotFailADownload();
        TwoDownloadsNeverShareAFile();
        ACancelLeavesAFinishedDownload();
        APauseBeforeTheTransferStartsHolds();
        APauseDuringTheSubtitleKeepsItForLater();
        AnAdoptedDownloadCarriesOnUnlessTheViewerPausedIt();
        ACopyWhoseReplacementIsBeingRemovedCanBeRemoved();
    }
}

void RunDownloadTransferStabilityTest()
{
    Apartment apartment;
    TemporaryDirectory temporary;
    DownloadHttpServer redirectTarget{ std::nullopt, static_cast<std::uint8_t>(0x52) };
    DownloadHttpServer server{ redirectTarget.RedirectTargetUrl() };
    std::mutex statusMutex;
    std::vector<DownloadStatus> statuses;
    // Keep callback state alive until after the engine's worker has stopped, even
    // if an assertion throws before the handler can be removed explicitly.
    TransferEngine engine{ temporary.Path() };
    engine.SetAccount(L"https://account.invalid", L"user-a");
    auto const token = engine.AddChangedHandler([&statusMutex, &statuses](DownloadRecord const& record)
    {
        if (record.Media.VideoId != L"movie:transfer-stability")
        {
            return;
        }
        std::scoped_lock const lock{ statusMutex };
        statuses.push_back(record.Status);
    });

    auto const first = engine.Start(MakeRequest(
        server.FirstUrl(),
        L"first.mkv",
        server.FirstBody().size(),
        false,
        SubtitleRequest{
            .Url = server.SubtitleUrl(),
            .Language = L"eng",
            .Id = L"subtitle:transfer-stability",
            .Headers = { { L"X-Halo-Test", L"subtitle-secret" } },
        }));
    Require(first.Status == DownloadStatus::Queued, "a new transfer did not begin queued");
    auto const firstDone = WaitForStatus(engine, first.JobId, DownloadStatus::Done);
    auto const firstFiles = engine.FilesForPlayback(firstDone.JobId);
    Require(ReadBytes(firstFiles.VideoPath) == server.FirstBody(), "the first downloaded video was corrupted");
    Require(firstFiles.SubtitlePath.has_value(), "the protected subtitle sidecar was not downloaded");
    Require(server.AuthorizedVideoRequests() == 1, "the protected video header was not sent exactly once");
    Require(server.AuthorizedSubtitleRequests() == 1, "the protected subtitle header was not sent exactly once");
    Require(server.RejectedRequests() == 0, "the loopback server rejected a protected request");

    engine.Pause(firstDone.JobId);
    engine.Resume(firstDone.JobId);
    Require(engine.FilesForPlayback(firstDone.JobId).VideoPath == firstFiles.VideoPath,
        "a terminal download was lost after a stale pause or resume action");

    {
        std::scoped_lock const lock{ statusMutex };
        Require(
            std::find(statuses.begin(), statuses.end(), DownloadStatus::Downloading) != statuses.end(),
            "the transfer never entered downloading state");
        Require(
            std::find(statuses.begin(), statuses.end(), DownloadStatus::Done) != statuses.end(),
            "the transfer never emitted completed state");
    }

    auto const index = ReadText(temporary.Path() / L"downloads-index.json");
    Require(index.find("127.0.0.1") == std::string::npos, "a source URL was persisted in the download index");
    Require(index.find("video-secret") == std::string::npos, "a video header was persisted in the download index");
    Require(index.find("subtitle-secret") == std::string::npos, "a subtitle header was persisted in the download index");

    engine.SetAccount(L"https://account.invalid", L"user-b");
    Require(engine.List().empty(), "one account could see another account's downloads");
    bool crossAccountRemovalRejected{};
    try
    {
        engine.Remove(firstDone.JobId);
    }
    catch (std::runtime_error const&)
    {
        crossAccountRemovalRejected = true;
    }
    Require(crossAccountRemovalRejected && std::filesystem::is_regular_file(firstFiles.VideoPath),
        "another account could delete a local download by job identifier");
    engine.SetAccount(L"https://account.invalid", L"user-a");
    Require(engine.List().size() == 1, "the original account could not recover its download list");

    auto const originalPath = firstFiles.VideoPath;
    auto const replacement = engine.Start(MakeRequest(
        server.ReplacementUrl(),
        L"replacement.mkv",
        server.ReplacementBody().size(),
        true));
    Require(
        std::filesystem::is_regular_file(originalPath),
        "the current file was removed before its replacement completed");
    auto const replacementDone = WaitForStatus(engine, replacement.JobId, DownloadStatus::Done);
    auto const replacementFiles = engine.FilesForPlayback(replacementDone.JobId);
    Require(
        ReadBytes(replacementFiles.VideoPath) == server.ReplacementBody(),
        "the replacement downloaded video was corrupted");
    Require(
        !std::filesystem::exists(originalPath),
        "the old file remained after its replacement completed successfully");
    Require(engine.List().size() == 1, "replacement left duplicate visible download records");
    Require(server.AuthorizedVideoRequests() == 2, "the replacement did not preserve protected video headers");
    Require(server.RejectedRequests() == 0, "the loopback server rejected a replacement request");

    auto const expired = engine.Start(MakeRequest(
        server.ExpiringUrl(),
        L"expiring.mkv",
        server.ReplacementBody().size(),
        false,
        std::nullopt,
        L"movie:same-url-replacement"));
    static_cast<void>(WaitForStatus(engine, expired.JobId, DownloadStatus::Failed));
    auto const refreshed = engine.Start(MakeRequest(
        server.ExpiringUrl(),
        L"expiring.mkv",
        server.ReplacementBody().size(),
        true,
        std::nullopt,
        L"movie:same-url-replacement",
        L"refreshed-secret"));
    auto const refreshedDone = WaitForStatus(engine, refreshed.JobId, DownloadStatus::Done);
    auto const refreshedFiles = engine.FilesForPlayback(refreshedDone.JobId);
    Require(
        ReadBytes(refreshedFiles.VideoPath) == server.ReplacementBody(),
        "same-URL replacement cleanup deleted the refreshed video");

    auto const cancelled = engine.Start(MakeRequest(
        server.CancelUrl(),
        L"cancel.mkv",
        8u * 1024u * 1024u,
        false,
        std::nullopt,
        L"movie:cancel-active"));
    static_cast<void>(WaitForStatus(engine, cancelled.JobId, DownloadStatus::Downloading));
    WaitForFile(cancelled.PartialPath());
    engine.Remove(cancelled.JobId);
    // The running transfer deletes its own files once it has stopped writing
    // them, so the files go shortly after Remove returns, not during it.
    Require(!ListsJob(engine, cancelled.JobId), "a removed transfer was still listed");
    WaitUntil([&cancelled]()
    {
        return !std::filesystem::exists(cancelled.PartialPath())
            && !std::filesystem::exists(cancelled.TargetPath())
            && !std::filesystem::exists(TablePathOf(cancelled));
    }, "cancelling an active transfer left downloaded data behind");

    auto const relativeRedirect = engine.Start(MakeRequest(
        server.RelativeRedirectUrl(),
        L"relative-redirect.mkv",
        server.FirstBody().size(),
        false,
        std::nullopt,
        L"movie:relative-redirect"));
    auto const relativeRedirectDone = WaitForStatus(
        engine, relativeRedirect.JobId, DownloadStatus::Done);
    Require(
        ReadBytes(engine.FilesForPlayback(relativeRedirectDone.JobId).VideoPath) == server.FirstBody(),
        "a same-origin relative redirect did not preserve the protected request");

    auto const crossOriginRedirect = engine.Start(MakeRequest(
        server.CrossOriginRedirectUrl(),
        L"cross-origin-redirect.mkv",
        redirectTarget.FirstBody().size(),
        false,
        std::nullopt,
        L"movie:cross-origin-redirect"));
    auto const crossOriginRedirectDone = WaitForStatus(
        engine, crossOriginRedirect.JobId, DownloadStatus::Done);
    Require(
        ReadBytes(engine.FilesForPlayback(crossOriginRedirectDone.JobId).VideoPath)
            == redirectTarget.FirstBody(),
        "a safe cross-origin redirect did not complete");
    Require(
        server.CrossOriginRedirectRequests() == 1
            && redirectTarget.RedirectTargetRequests() == 1,
        "a protected source header leaked to a cross-origin redirect target");

    {
        TemporaryDirectory resumeTemporary;
        DownloadHttpServer resumeTarget{ std::nullopt, static_cast<std::uint8_t>(0x62) };
        DownloadHttpServer resumeServer{ resumeTarget.RedirectTargetUrl() };
        DownloadIndexStore seed{ resumeTemporary.Path() };
        auto resumed = MakeRequest(
            resumeServer.CrossOriginRedirectUrl(),
            L"cross-origin-resume.mkv",
            resumeTarget.FirstBody().size(),
            false,
            std::nullopt,
            L"movie:cross-origin-resume");
        DownloadRecord record{
            .JobId = L"0123456789abcdef0123456789abcdef",
            .AccountKey = MakeAccountKey(L"https://account.invalid", L"user-a"),
            .Media = {
                .VideoId = L"movie:cross-origin-resume",
                .ItemId = L"movie:cross-origin-resume",
                .MediaType = L"movie",
                .Title = L"Cross-origin resume",
                .VideoSize = resumeTarget.FirstBody().size(),
            },
            .FileName = L"cross-origin-resume.mkv",
            .RootPath = resumeTemporary.Path() / L"downloads",
            .Status = DownloadStatus::Paused,
            .TotalBytes = resumeTarget.FirstBody().size(),
            .DownloadedBytes = 32u * 1024u,
            .Validator = L"\"halo-stability\"",
            .SourceFingerprint = Sha256Hex(resumed.Request.Url),
            .ExplicitPause = true,
            .CreatedAt = 1,
            .UpdatedAt = 1,
        };
        std::filesystem::create_directories(record.RootPath);
        WriteBytes(
            record.PartialPath(),
            std::vector<std::uint8_t>(
                resumeServer.FirstBody().begin(),
                resumeServer.FirstBody().begin() + static_cast<std::ptrdiff_t>(record.DownloadedBytes)));
        seed.Apply({ record });
        RequestVault vault{ resumeTemporary.Path() / L"download-requests" };
        vault.Write(record.JobId, std::move(resumed.Request));

        TransferEngine resumedEngine{ resumeTemporary.Path() };
        resumedEngine.SetAccount(L"https://account.invalid", L"user-a");
        resumedEngine.Resume(record.JobId);
        auto const completed = WaitForStatus(resumedEngine, record.JobId, DownloadStatus::Done);
        Require(
            ReadBytes(resumedEngine.FilesForPlayback(completed.JobId).VideoPath) == resumeTarget.FirstBody(),
            "a cross-origin resumed transfer appended redirected bytes to the old prefix");
        Require(
            resumeTarget.RedirectTargetRangeRequests() == 1
                && resumeTarget.RedirectTargetRequests() == 2,
            "a cross-origin resume did not retry after the redirected ranged response");
    }

    auto const redirectLoop = engine.Start(MakeRequest(
        server.RedirectLoopUrl(),
        L"redirect-loop.mkv",
        server.FirstBody().size(),
        false,
        std::nullopt,
        L"movie:redirect-loop"));
    auto const redirectLoopFailed = WaitForStatus(
        engine, redirectLoop.JobId, DownloadStatus::Failed);
    Require(
        redirectLoopFailed.Failure == DownloadFailureCode::SourceRejected,
        "an unbounded redirect chain was not rejected safely");

    engine.RemoveChangedHandler(token);

    RunSegmentedTransferTests();
    RunDownloadEngineSafetyTests();
}
