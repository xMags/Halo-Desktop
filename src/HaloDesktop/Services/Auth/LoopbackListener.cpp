#include "pch.h"
#include "Services/Auth/LoopbackListener.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>

namespace
{
    constexpr std::uint16_t CallbackPort = 17871;
    constexpr std::string_view SuccessPage =
        "<!doctype html><html><head><meta charset=\"utf-8\"><title>Halo</title></head>"
        "<body style=\"background:#0a0c11;color:#f4f6fb;font-family:'Segoe UI',sans-serif;"
        "display:grid;place-items:center;height:100vh;margin:0\"><div style=\"text-align:center\">"
        "<h2>Signed in to Halo</h2><p style=\"color:#8b93a5\">You can close this tab and return to the app."
        "</p></div></body></html>";

    void SendResponse(SOCKET socket, std::string const& response) noexcept
    {
        auto const* data = response.data();
        auto remaining = response.size();
        while (remaining > 0)
        {
            auto const chunk = remaining > static_cast<std::size_t>(INT_MAX)
                ? INT_MAX
                : static_cast<int>(remaining);
            auto const sent = send(socket, data, chunk, 0);
            if (sent <= 0)
            {
                return;
            }
            data += sent;
            remaining -= static_cast<std::size_t>(sent);
        }
    }

    std::string RequestPath(std::string_view request)
    {
        auto const lineEnd = request.find("\r\n");
        auto const firstLine = request.substr(0, lineEnd);
        auto const firstSpace = firstLine.find(' ');
        auto const secondSpace = firstSpace == std::string_view::npos
            ? std::string_view::npos
            : firstLine.find(' ', firstSpace + 1);
        if (firstSpace == std::string_view::npos || secondSpace == std::string_view::npos
            || firstLine.substr(0, firstSpace) != "GET")
        {
            return "/";
        }
        return std::string{ firstLine.substr(firstSpace + 1, secondSpace - firstSpace - 1) };
    }

    void WakeListener() noexcept
    {
        auto const client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (client == INVALID_SOCKET)
        {
            return;
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(CallbackPort);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != SOCKET_ERROR)
        {
            constexpr std::string_view cancel =
                "GET /cancel HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
            static_cast<void>(send(client, cancel.data(), static_cast<int>(cancel.size()), 0));
        }
        closesocket(client);
    }
}

namespace HaloDesktop::Services::Auth
{
    struct LoopbackListener::State final
    {
        explicit State(std::chrono::seconds requestedTimeout)
            : timeout(requestedTimeout)
        {
            try
            {
                WSADATA data{};
                if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
                {
                    throw std::runtime_error{ "Windows could not initialize the sign-in listener." };
                }
                winsockStarted = true;

                listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                if (listener == INVALID_SOCKET)
                {
                    throw std::runtime_error{ "Windows could not create the sign-in listener." };
                }

                BOOL exclusive = TRUE;
                // Winsock's byte-buffer ABI requires these narrow pointer views.
                if (setsockopt(
                    listener,
                    SOL_SOCKET,
                    SO_EXCLUSIVEADDRUSE,
                    reinterpret_cast<char const*>(&exclusive),
                    sizeof(exclusive)) == SOCKET_ERROR)
                {
                    throw std::runtime_error{ "Windows could not secure the sign-in listener." };
                }

                sockaddr_in address{};
                address.sin_family = AF_INET;
                address.sin_port = htons(CallbackPort);
                address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR
                    || listen(listener, SOMAXCONN) == SOCKET_ERROR)
                {
                    throw std::runtime_error{ "The Halo sign-in callback port is already in use." };
                }

                worker = std::jthread{ [this](std::stop_token stopToken) { Run(stopToken); } };
            }
            catch (...)
            {
                if (listener != INVALID_SOCKET)
                {
                    closesocket(listener);
                    listener = INVALID_SOCKET;
                }
                if (winsockStarted)
                {
                    WSACleanup();
                    winsockStarted = false;
                }
                throw;
            }
        }

        ~State()
        {
            Cancel();
            if (winsockStarted)
            {
                WSACleanup();
            }
        }

        void Complete(winrt::hstring const& path)
        {
            if (!completed.exchange(true))
            {
                completion.set(path);
            }
        }

        void Fail(char const* message)
        {
            if (!completed.exchange(true))
            {
                completion.set_exception(std::make_exception_ptr(std::runtime_error{ message }));
            }
        }

        void Cancel() noexcept
        {
            std::scoped_lock const lock{ cancelMutex };
            Fail("Halo sign-in was cancelled.");
            worker.request_stop();
            InterruptActiveClient();
            WakeListener();
            if (worker.joinable() && worker.get_id() != std::this_thread::get_id())
            {
                worker.join();
            }
            if (listener != INVALID_SOCKET)
            {
                closesocket(listener);
                listener = INVALID_SOCKET;
            }
        }

        void InterruptActiveClient() noexcept
        {
            std::scoped_lock const lock{ clientMutex };
            if (activeClient != INVALID_SOCKET)
            {
                shutdown(activeClient, SD_BOTH);
            }
        }

        void CloseClient(SOCKET client) noexcept
        {
            {
                std::scoped_lock const lock{ clientMutex };
                if (activeClient == client)
                {
                    activeClient = INVALID_SOCKET;
                }
            }
            shutdown(client, SD_BOTH);
            closesocket(client);
        }

        void Run(std::stop_token stopToken)
        {
            auto const deadline = std::chrono::steady_clock::now() + timeout;
            while (!stopToken.stop_requested())
            {
                if (std::chrono::steady_clock::now() >= deadline)
                {
                    Fail("Halo sign-in timed out.");
                    return;
                }

                fd_set sockets;
                FD_ZERO(&sockets);
                FD_SET(listener, &sockets);
                timeval wait{ 0, 250000 };
                auto const selected = select(0, &sockets, nullptr, nullptr, &wait);
                if (selected == SOCKET_ERROR)
                {
                    if (!stopToken.stop_requested())
                    {
                        Fail("The Halo sign-in listener stopped unexpectedly.");
                    }
                    return;
                }
                if (selected == 0)
                {
                    continue;
                }

                sockaddr_in remote{};
                int remoteSize = sizeof(remote);
                auto const client = accept(listener, reinterpret_cast<sockaddr*>(&remote), &remoteSize);
                if (client == INVALID_SOCKET)
                {
                    continue;
                }
                {
                    std::scoped_lock const lock{ clientMutex };
                    activeClient = client;
                }
                if (stopToken.stop_requested())
                {
                    CloseClient(client);
                    return;
                }

                DWORD receiveTimeout = 5000;
                setsockopt(
                    client,
                    SOL_SOCKET,
                    SO_RCVTIMEO,
                    reinterpret_cast<char const*>(&receiveTimeout),
                    sizeof(receiveTimeout));
                std::array<char, 8192> buffer{};
                auto const received = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
                auto const path = received > 0
                    ? RequestPath(std::string_view{ buffer.data(), static_cast<std::size_t>(received) })
                    : std::string{ "/" };

                auto const isCancel = path == "/cancel" || path.starts_with("/cancel?");
                if (isCancel)
                {
                    SendResponse(client, "HTTP/1.1 200 OK\r\ncontent-length: 0\r\nconnection: close\r\n\r\n");
                    CloseClient(client);
                    Fail("Halo sign-in was cancelled.");
                    return;
                }
                auto const isCallback = path == "/callback" || path.starts_with("/callback?");
                if (!isCallback)
                {
                    SendResponse(client, "HTTP/1.1 404 Not Found\r\ncontent-length: 0\r\nconnection: close\r\n\r\n");
                    CloseClient(client);
                    continue;
                }

                auto const response = std::string{ "HTTP/1.1 200 OK\r\ncontent-type: text/html; charset=utf-8\r\ncontent-length: " }
                    + std::to_string(SuccessPage.size())
                    + "\r\nconnection: close\r\n\r\n"
                    + std::string{ SuccessPage };
                SendResponse(client, response);
                CloseClient(client);
                Complete(winrt::to_hstring(path));
                return;
            }
        }

        std::chrono::seconds timeout;
        std::mutex cancelMutex;
        std::mutex clientMutex;
        concurrency::task_completion_event<winrt::hstring> completion;
        std::atomic_bool completed{};
        SOCKET listener{ INVALID_SOCKET };
        SOCKET activeClient{ INVALID_SOCKET };
        std::jthread worker;
        bool winsockStarted{};
    };

    LoopbackListener::LoopbackListener(std::chrono::seconds timeout)
        : m_state(std::make_unique<State>(timeout))
    {
    }

    LoopbackListener::~LoopbackListener() = default;

    concurrency::task<winrt::hstring> LoopbackListener::WaitAsync()
    {
        return concurrency::create_task(m_state->completion);
    }

    void LoopbackListener::Cancel() noexcept
    {
        m_state->Cancel();
    }
}
