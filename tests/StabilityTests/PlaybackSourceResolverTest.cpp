// Winsock must be declared before anything can reach windows.h.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "PlaybackSourceResolverTest.h"

#include "Playback/PlaybackSourceResolver.h"
#include "Security/ProtectedRedirect.h"

#include <array>
#include <atomic>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace
{
    using HaloDesktop::Playback::PlaybackSource;
    using HaloDesktop::Playback::ResolvePlaybackSource;

    constexpr std::wstring_view SecretName = L"X-Halo-Secret";
    constexpr std::wstring_view SecretValue = L"source-token";

    void Require(bool condition, char const* message)
    {
        if (!condition)
        {
            throw std::runtime_error{ message };
        }
    }

    void SendAll(SOCKET client, std::string const& payload)
    {
        std::size_t sent{};
        while (sent < payload.size())
        {
            auto const count = send(
                client,
                payload.data() + sent,
                static_cast<int>(payload.size() - sent),
                0);
            if (count <= 0)
            {
                return;
            }
            sent += static_cast<std::size_t>(count);
        }
    }

    // A loopback origin. Two of these on different ports are two origins, which
    // is what makes the cross-origin case deterministic without depending on how
    // a hostname happens to resolve.
    class ProbeServer final
    {
    public:
        ProbeServer()
        {
            WSADATA data{};
            if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
            {
                throw std::runtime_error{ "Winsock could not start for the resolver test." };
            }
            m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (m_socket == INVALID_SOCKET)
            {
                WSACleanup();
                throw std::runtime_error{ "The resolver test socket could not be created." };
            }
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port = 0;
            if (bind(m_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR
                || listen(m_socket, SOMAXCONN) == SOCKET_ERROR)
            {
                closesocket(m_socket);
                WSACleanup();
                throw std::runtime_error{ "The resolver test server could not listen." };
            }
            int addressSize = sizeof(address);
            if (getsockname(m_socket, reinterpret_cast<sockaddr*>(&address), &addressSize) == SOCKET_ERROR)
            {
                closesocket(m_socket);
                WSACleanup();
                throw std::runtime_error{ "The resolver test port could not be read." };
            }
            m_port = ntohs(address.sin_port);
            m_thread = std::jthread([this]() { Run(); });
        }

        ~ProbeServer()
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
            }
            WSACleanup();
        }

        ProbeServer(ProbeServer const&) = delete;
        ProbeServer& operator=(ProbeServer const&) = delete;

        [[nodiscard]] std::wstring Url(std::wstring_view path) const
        {
            return L"http://127.0.0.1:" + std::to_wstring(m_port) + std::wstring{ path };
        }

        // Where /cross sends the caller. Set before any request is made.
        void RedirectCrossTo(std::wstring url)
        {
            m_crossTarget = std::move(url);
        }

        [[nodiscard]] int Requests() const noexcept { return m_requests.load(); }
        [[nodiscard]] int FinalRequests() const noexcept { return m_finalRequests.load(); }
        [[nodiscard]] int FinalRequestsCarryingSecret() const noexcept
        {
            return m_finalRequestsCarryingSecret.load();
        }

    private:
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
            static_cast<void>(connect(wake, reinterpret_cast<sockaddr*>(&address), sizeof(address)));
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
            while (request.find("\r\n\r\n") == std::string::npos && request.size() < 64u * 1024u)
            {
                auto const count = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
                if (count <= 0)
                {
                    break;
                }
                request.append(buffer.data(), static_cast<std::size_t>(count));
            }
            if (request.empty())
            {
                return;
            }
            ++m_requests;
            auto const carriesSecret =
                request.find("\r\nX-Halo-Secret: source-token\r\n") != std::string::npos;

            if (request.starts_with("GET /cross ") && !m_crossTarget.empty())
            {
                std::string location;
                for (auto const character : m_crossTarget)
                {
                    location.push_back(static_cast<char>(character));
                }
                SendAll(client,
                    "HTTP/1.1 302 Found\r\nLocation: " + location
                    + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                return;
            }
            if (request.starts_with("GET /same "))
            {
                SendAll(client,
                    "HTTP/1.1 302 Found\r\nLocation: /final"
                    "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                return;
            }
            if (request.starts_with("GET /final "))
            {
                ++m_finalRequests;
                if (carriesSecret)
                {
                    ++m_finalRequestsCarryingSecret;
                }
                SendAll(client,
                    "HTTP/1.1 206 Partial Content\r\nContent-Type: application/octet-stream"
                    "\r\nContent-Range: bytes 0-0/1024\r\nContent-Length: 1"
                    "\r\nConnection: close\r\n\r\nH");
                return;
            }
            SendAll(client, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        }

        SOCKET m_socket{ INVALID_SOCKET };
        std::uint16_t m_port{};
        std::wstring m_crossTarget;
        std::atomic_int m_requests{};
        std::atomic_int m_finalRequests{};
        std::atomic_int m_finalRequestsCarryingSecret{};
        std::atomic_bool m_stopping{};
        std::jthread m_thread;
    };

    PlaybackSource Protected(std::wstring location)
    {
        return PlaybackSource{
            .Location = std::move(location),
            .Headers = { { std::wstring{ SecretName }, std::wstring{ SecretValue } } },
        };
    }

    void TestRedirectPolicy()
    {
        using HaloDesktop::Security::MaximumProtectedRedirects;
        using HaloDesktop::Security::NextRedirectTarget;

        auto const relative = NextRedirectTarget(L"https://host.test/a/b", L"../c", 0);
        Require(relative && relative->SameOrigin && relative->Url == L"https://host.test/c",
            "a relative redirect on the same origin was not resolved");

        auto const otherHost = NextRedirectTarget(L"https://host.test/a", L"https://cdn.test/a", 0);
        Require(otherHost && !otherHost->SameOrigin,
            "a redirect to another host was treated as the same origin");

        auto const otherPort = NextRedirectTarget(L"https://host.test/a", L"https://host.test:8443/a", 0);
        Require(otherPort && !otherPort->SameOrigin,
            "a redirect to another port was treated as the same origin");

        Require(!NextRedirectTarget(L"https://host.test/a", L"http://host.test/a", 0),
            "a redirect out of https was followed");
        Require(!NextRedirectTarget(L"https://host.test/a", L"file:///c:/secret", 0),
            "a redirect to a non-http scheme was followed");
        Require(!NextRedirectTarget(L"https://host.test/a", L"", 0),
            "an empty Location was followed");
        Require(!NextRedirectTarget(L"https://host.test/a", L"https://host.test/b",
                MaximumProtectedRedirects),
            "the redirect hop limit was not enforced");
    }

    void TestCrossOriginDropsHeaders()
    {
        ProbeServer source;
        ProbeServer cdn;
        source.RedirectCrossTo(cdn.Url(L"/final"));

        auto const resolved = ResolvePlaybackSource(Protected(source.Url(L"/cross")));
        Require(resolved.Location == cdn.Url(L"/final"),
            "a cross-origin redirect did not settle on the redirect target");
        Require(resolved.Headers.empty(),
            "protected headers survived a cross-origin redirect");
        Require(cdn.FinalRequests() == 1 && cdn.FinalRequestsCarryingSecret() == 0,
            "the probe itself handed the protected header to the other origin");
    }

    void TestSameOriginKeepsHeaders()
    {
        ProbeServer source;

        auto const resolved = ResolvePlaybackSource(Protected(source.Url(L"/same")));
        Require(resolved.Location == source.Url(L"/final"),
            "a same-origin redirect did not settle on the redirect target");
        Require(resolved.Headers.size() == 1
                && resolved.Headers.front().Name == SecretName
                && resolved.Headers.front().Value == SecretValue,
            "protected headers were dropped on a same-origin redirect");
        Require(source.FinalRequestsCarryingSecret() == 1,
            "the same-origin probe did not carry the protected header");
    }

    void TestSourceWithoutHeadersIsNotProbed()
    {
        ProbeServer source;
        ProbeServer cdn;
        source.RedirectCrossTo(cdn.Url(L"/final"));

        auto const location = source.Url(L"/cross");
        auto const resolved = ResolvePlaybackSource(PlaybackSource{ .Location = location });
        Require(resolved.Location == location && resolved.Headers.empty(),
            "a source with nothing to leak was rewritten");
        Require(source.Requests() == 0,
            "a source with nothing to leak was probed anyway");
    }

    void TestUnreachableSourceIsLeftAlone()
    {
        std::wstring location;
        {
            // Take a port and give it straight back, so nothing is listening on it.
            ProbeServer closing;
            location = closing.Url(L"/final");
        }

        auto const resolved = ResolvePlaybackSource(Protected(location));
        Require(resolved.Location == location && resolved.Headers.size() == 1,
            "an unreachable source was not left for the engine to try itself");
    }
}

void RunPlaybackSourceResolverTest()
{
    TestRedirectPolicy();
    TestCrossOriginDropsHeaders();
    TestSameOriginKeepsHeaders();
    TestSourceWithoutHeadersIsNotProbed();
    TestUnreachableSourceIsLeftAlone();
}
