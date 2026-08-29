#include "DownloadTransferTest.h"

#include "Services/Downloads/TransferEngine.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
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

    [[nodiscard]] std::string ReadText(std::filesystem::path const& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            throw std::runtime_error{ "The transfer test could not read the download index." };
        }
        return {
            std::istreambuf_iterator<char>{ input },
            std::istreambuf_iterator<char>{},
        };
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
    Require(
        !std::filesystem::exists(cancelled.PartialPath())
            && !std::filesystem::exists(cancelled.TargetPath()),
        "cancelling an active transfer left downloaded data behind");

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
}
