#include <winsock2.h>
#include <ws2tcpip.h>

#include "ApiClientAuthTest.h"

#include "Api/ApiClient.h"
#include "Api/ApiError.h"
#include "Api/HttpExecutor.h"
#include "Services/Auth/ITokenProvider.h"
#include "Services/Auth/LocalAuthSession.h"
#include "Services/Auth/SessionStore.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    void Require(bool condition, char const* message)
    {
        if (!condition)
        {
            throw std::runtime_error{ message };
        }
    }

    struct CannedResponse final
    {
        int Status{ 200 };
        std::string Body;
        std::string ContentType{ "application/json" };
        // Extra header lines, each ending in CRLF (a redirect's Location).
        std::string Headers;
    };

    struct SeenRequest final
    {
        std::string Method;
        std::string Path;
        // Empty when the request carried no bearer token.
        std::string Bearer;
        std::string Body;
    };

    // One request per connection, answered by a function of the path and the
    // bearer token, the way the reference test's fake fetch answers.
    class AuthHttpServer final
    {
    public:
        using Responder = std::function<CannedResponse(std::string const& path, std::string const& bearer)>;

        explicit AuthHttpServer(Responder responder)
            : m_responder(std::move(responder))
        {
            WSADATA data{};
            if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
            {
                throw std::runtime_error{ "Winsock could not start for the auth test." };
            }
            m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port = 0;
            int addressSize = sizeof(address);
            if (m_socket == INVALID_SOCKET
                || bind(m_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR
                || listen(m_socket, SOMAXCONN) == SOCKET_ERROR
                || getsockname(m_socket, reinterpret_cast<sockaddr*>(&address), &addressSize) == SOCKET_ERROR)
            {
                if (m_socket != INVALID_SOCKET) closesocket(m_socket);
                WSACleanup();
                throw std::runtime_error{ "The auth test server could not listen." };
            }
            m_port = ntohs(address.sin_port);
            m_thread = std::jthread([this]() { Run(); });
        }

        ~AuthHttpServer()
        {
            m_stopping.store(true);
            WakeListener();
            if (m_thread.joinable()) m_thread.join();
            closesocket(m_socket);
            WSACleanup();
        }

        AuthHttpServer(AuthHttpServer const&) = delete;
        AuthHttpServer& operator=(AuthHttpServer const&) = delete;

        [[nodiscard]] winrt::hstring BaseUrl() const
        {
            return winrt::hstring{ L"http://127.0.0.1:" + std::to_wstring(m_port) };
        }

        [[nodiscard]] std::vector<SeenRequest> Requests() const
        {
            std::scoped_lock const lock{ m_mutex };
            return m_requests;
        }

        void Respond(Responder responder)
        {
            std::scoped_lock const lock{ m_mutex };
            m_responder = std::move(responder);
        }

    private:
        void WakeListener() const noexcept
        {
            auto const wake = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (wake == INVALID_SOCKET) return;
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
                if (client == INVALID_SOCKET) continue;
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
                if (count <= 0) break;
                request.append(buffer.data(), static_cast<std::size_t>(count));
            }
            if (request.find("\r\n\r\n") == std::string::npos) return;

            auto const pathStart = request.find(' ');
            auto const pathEnd = request.find(' ', pathStart + 1);
            if (pathStart == std::string::npos || pathEnd == std::string::npos) return;
            auto const method = request.substr(0, pathStart);
            auto const path = request.substr(pathStart + 1, pathEnd - pathStart - 1);
            auto const headEnd = request.find("\r\n\r\n");
            std::string body = request.substr(headEnd + 4);
            std::size_t contentLength{};
            constexpr std::string_view lengthHeader = "\r\nContent-Length: ";
            if (auto const found = request.find(lengthHeader); found != std::string::npos && found < headEnd)
            {
                contentLength = static_cast<std::size_t>(std::stoul(request.substr(found + lengthHeader.size())));
            }
            while (body.size() < contentLength && body.size() < 64u * 1024u)
            {
                auto const count = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
                if (count <= 0) break;
                body.append(buffer.data(), static_cast<std::size_t>(count));
            }

            std::string bearer;
            constexpr std::string_view authorization = "\r\nAuthorization: Bearer ";
            if (auto const found = request.find(authorization); found != std::string::npos)
            {
                auto const valueStart = found + authorization.size();
                bearer = request.substr(valueStart, request.find("\r\n", valueStart) - valueStart);
            }

            CannedResponse response;
            {
                std::scoped_lock const lock{ m_mutex };
                m_requests.push_back({ method, path, bearer, body });
                response = m_responder(path, bearer);
            }
            auto const text = "HTTP/1.1 " + std::to_string(response.Status) + " Test\r\nContent-Type: "
                + response.ContentType + "\r\nContent-Length: " + std::to_string(response.Body.size())
                + "\r\n" + response.Headers + "Connection: close\r\n\r\n" + response.Body;
            std::size_t sent{};
            while (sent < text.size())
            {
                auto const count = send(client, text.data() + sent, static_cast<int>(text.size() - sent), 0);
                if (count <= 0) return;
                sent += static_cast<std::size_t>(count);
            }
        }

        mutable std::mutex m_mutex;
        Responder m_responder;
        std::vector<SeenRequest> m_requests;
        SOCKET m_socket{ INVALID_SOCKET };
        std::uint16_t m_port{};
        std::atomic_bool m_stopping{};
        std::jthread m_thread;
    };

    // A provider whose refresh hands out the next token (empty = the identity
    // provider rejected the refresh), as the reference test's provider does.
    class FakeTokenProvider final : public HaloDesktop::Services::Auth::ITokenProvider
    {
    public:
        using RefreshResult = std::function<std::optional<winrt::hstring>()>;

        explicit FakeTokenProvider(RefreshResult refresh)
            : m_refresh(std::move(refresh))
        {
        }

        [[nodiscard]] concurrency::task<std::optional<winrt::hstring>> AccessTokenAsync() override
        {
            std::scoped_lock const lock{ m_mutex };
            return concurrency::task_from_result(std::optional<winrt::hstring>{ m_token });
        }

        [[nodiscard]] concurrency::task<std::optional<winrt::hstring>> RefreshAccessTokenAsync() override
        {
            ++m_refreshes;
            try
            {
                auto next = m_refresh();
                if (next)
                {
                    std::scoped_lock const lock{ m_mutex };
                    m_token = *next;
                }
                return concurrency::task_from_result(std::move(next));
            }
            catch (...)
            {
                return concurrency::task_from_exception<std::optional<winrt::hstring>>(std::current_exception());
            }
        }

        [[nodiscard]] concurrency::task<void> RejectSessionAsync(std::uint64_t) override
        {
            ++m_rejections;
            return concurrency::task_from_result();
        }

        [[nodiscard]] std::uint64_t SessionGeneration() const noexcept override
        {
            return 1;
        }

        [[nodiscard]] HaloDesktop::Services::Auth::SessionTarget CurrentTarget() const override
        {
            std::scoped_lock const lock{ m_mutex };
            return { .Generation = 1, .ServerUrl = m_serverUrl };
        }

        void ServeFrom(std::optional<winrt::hstring> serverUrl)
        {
            std::scoped_lock const lock{ m_mutex };
            m_serverUrl = std::move(serverUrl);
        }

        [[nodiscard]] int Refreshes() const noexcept { return m_refreshes.load(); }
        [[nodiscard]] int Rejections() const noexcept { return m_rejections.load(); }

    private:
        RefreshResult m_refresh;
        mutable std::mutex m_mutex;
        winrt::hstring m_token{ L"access-1" };
        std::optional<winrt::hstring> m_serverUrl;
        std::atomic_int m_refreshes{};
        std::atomic_int m_rejections{};
    };

    constexpr std::string_view Me = R"({"id":"u1","username":"kenneth","isAdmin":false,"createdAt":1})";

    CannedResponse Json(int status, std::string_view body)
    {
        return { status, std::string{ body } };
    }

    std::optional<winrt::hresult> FailureOf(std::function<void()> const& call)
    {
        try
        {
            call();
        }
        catch (winrt::hresult_error const& error)
        {
            return error.code();
        }
        return std::nullopt;
    }

    bool IsHttp401(std::optional<winrt::hresult> const& code)
    {
        return code && HaloDesktop::Api::ApiError::HttpStatus(*code).value_or(0) == 401;
    }

    struct Harness final
    {
        explicit Harness(AuthHttpServer::Responder responder, FakeTokenProvider::RefreshResult refresh)
            : Server(std::move(responder)),
              Tokens(std::make_shared<FakeTokenProvider>(std::move(refresh))),
              Client(Server.BaseUrl(), std::make_shared<HaloDesktop::Api::HttpExecutor>(), Tokens)
        {
        }

        AuthHttpServer Server;
        std::shared_ptr<FakeTokenProvider> Tokens;
        HaloDesktop::Api::ApiClient Client;
    };

    std::optional<winrt::hstring> RefreshTo(wchar_t const* token)
    {
        return winrt::hstring{ token };
    }

    void TestRefreshedOnceAndRetried()
    {
        Harness harness{
            [](std::string const&, std::string const& bearer)
            {
                return bearer == "access-2" ? Json(200, Me) : Json(401, R"({"error":"expired"})");
            },
            []() { return RefreshTo(L"access-2"); } };

        auto const me = harness.Client.GetMeAsync().get();
        auto const requests = harness.Server.Requests();
        Require(me.Id == L"u1", "a refreshed request did not return the account");
        Require(requests.size() == 2 && requests[0].Bearer == "access-1" && requests[1].Bearer == "access-2",
            "a 401 was not retried once with the new token");
        Require(harness.Tokens->Refreshes() == 1 && harness.Tokens->Rejections() == 0,
            "a refreshed request refreshed more than once or ended the session");
    }

    void TestRejectedRefreshEndsSession()
    {
        Harness harness{
            [](std::string const&, std::string const&) { return Json(401, R"({"error":"nope"})"); },
            []() { return std::optional<winrt::hstring>{}; } };

        auto const failure = FailureOf([&harness]() { static_cast<void>(harness.Client.GetMeAsync().get()); });
        Require(failure && HaloDesktop::Api::ApiError::IsSessionRejected(*failure),
            "a rejected refresh did not end the session");
        Require(harness.Tokens->Rejections() == 1 && harness.Server.Requests().size() == 1,
            "a rejected refresh was retried or rejected more than once");
    }

    void TestUnauthorizedAfterRefreshKeepsSession()
    {
        // The refresh proved the session alive; the server only refused the
        // new token (right after a signing-key rotation, say). Signing out here
        // would also pause every active download.
        Harness harness{
            [](std::string const&, std::string const&) { return Json(401, R"({"error":"unknown signing key"})"); },
            []() { return RefreshTo(L"access-2"); } };

        auto const failure = FailureOf([&harness]() { static_cast<void>(harness.Client.GetMeAsync().get()); });
        Require(IsHttp401(failure), "a 401 after a successful refresh did not fail with its 401");
        Require(harness.Tokens->Rejections() == 0, "a 401 after a successful refresh ended the session");
        Require(harness.Tokens->Refreshes() == 1 && harness.Server.Requests().size() == 2,
            "a 401 after a successful refresh was retried again");

        // The same rule on the image path.
        auto const avatarFailure = FailureOf([&harness]() { static_cast<void>(harness.Client.GetAvatarAsync().get()); });
        Require(IsHttp401(avatarFailure) && harness.Tokens->Rejections() == 0,
            "the picture request ended a live session");
    }

    void TestLaterRequestSucceeds()
    {
        auto accepts = std::make_shared<std::atomic_bool>(false);
        Harness harness{
            [accepts](std::string const&, std::string const& bearer)
            {
                return bearer == "access-2" && accepts->load() ? Json(200, Me) : Json(401, "{}");
            },
            []() { return RefreshTo(L"access-2"); } };

        Require(IsHttp401(FailureOf([&harness]() { static_cast<void>(harness.Client.GetMeAsync().get()); })),
            "the first request did not fail while the server refused the new token");
        accepts->store(true);
        Require(harness.Client.GetMeAsync().get().Id == L"u1", "a later request failed once the server accepted the token");
        Require(harness.Tokens->Rejections() == 0, "the session was ended along the way");
    }

    void TestRefreshTransportFailureKeepsSession()
    {
        Harness harness{
            [](std::string const&, std::string const&) { return Json(401, "{}"); },
            []() -> std::optional<winrt::hstring>
            {
                throw winrt::hresult_error{ HaloDesktop::Api::ApiError::Transport, L"network down" };
            } };

        auto const failure = FailureOf([&harness]() { static_cast<void>(harness.Client.GetMeAsync().get()); });
        Require(failure && HaloDesktop::Api::ApiError::IsTransport(*failure),
            "a network failure during refresh did not propagate");
        Require(harness.Tokens->Rejections() == 0, "a network failure during refresh ended the session");
    }

    void TestAvatarAndPlanRefusal()
    {
        auto picture = std::make_shared<std::atomic_bool>(false);
        Harness harness{
            [picture](std::string const& path, std::string const&)
            {
                if (path == "/auth/avatar")
                {
                    return picture->load()
                        ? CannedResponse{ 200, "PNGDATA", "image/png" }
                        : Json(404, R"({"error":"no picture"})");
                }
                return Json(403, R"({"code":"plan_required","error":"Sign-in needs Premium."})");
            },
            []() { return RefreshTo(L"access-2"); } };

        Require(!harness.Client.GetAvatarAsync().get(), "a missing picture was not read as none");
        picture->store(true);
        auto const bytes = harness.Client.GetAvatarAsync().get();
        Require(bytes && std::string(bytes->begin(), bytes->end()) == "PNGDATA", "the picture bytes were not returned");

        winrt::hstring message;
        std::optional<winrt::hresult> code;
        try
        {
            static_cast<void>(harness.Client.GetMeAsync().get());
        }
        catch (winrt::hresult_error const& error)
        {
            code = error.code();
            message = error.message();
        }
        Require(code && HaloDesktop::Api::ApiError::IsPlanRefusal(*code)
                && HaloDesktop::Api::ApiError::HttpStatus(*code).value_or(0) == 403
                && message == L"Sign-in needs Premium.",
            "a plan refusal did not carry the server's sentence and status");
        Require(harness.Tokens->Rejections() == 0 && harness.Tokens->Refreshes() == 0,
            "a plan refusal was treated as an authentication failure");
    }

    // The session's own server answers every request made for it: a username
    // and password session opened on a self-hosted server never sends its
    // token to the built-in one, and the built-in session never to it.
    void TestRequestsFollowTheSessionServer()
    {
        AuthHttpServer builtIn{ [](std::string const&, std::string const&) { return Json(500, R"({"error":"wrong server"})"); } };
        AuthHttpServer own{ [](std::string const&, std::string const& bearer)
        {
            return bearer == "access-1" ? Json(200, Me) : Json(401, "{}");
        } };
        auto const tokens = std::make_shared<FakeTokenProvider>([]() { return RefreshTo(L"access-2"); });
        tokens->ServeFrom(own.BaseUrl());
        HaloDesktop::Api::ApiClient client{ builtIn.BaseUrl(), std::make_shared<HaloDesktop::Api::HttpExecutor>(), tokens };

        Require(client.GetMeAsync().get().Id == L"u1", "a self-hosted session's request did not reach its server");
        Require(builtIn.Requests().empty() && own.Requests().size() == 1,
            "a self-hosted session's token reached the built-in server");

        tokens->ServeFrom(std::nullopt);
        auto const failure = FailureOf([&client]() { static_cast<void>(client.GetMeAsync().get()); });
        Require(failure && HaloDesktop::Api::ApiError::HttpStatus(*failure).value_or(0) == 500,
            "the built-in session's request did not reach the built-in server");
        Require(own.Requests().size() == 1, "the built-in session's token reached a self-hosted server");
    }

    class TemporaryFolder final
    {
    public:
        TemporaryFolder()
            : m_path(std::filesystem::temp_directory_path()
                / (L"HaloDesktop-SignIn-" + std::to_wstring(GetCurrentProcessId()) + L"-"
                    + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++s_counter)))
        {
            std::filesystem::create_directories(m_path);
        }

        ~TemporaryFolder()
        {
            std::error_code ignored;
            std::filesystem::remove_all(m_path, ignored);
        }

        TemporaryFolder(TemporaryFolder const&) = delete;
        TemporaryFolder& operator=(TemporaryFolder const&) = delete;

        [[nodiscard]] std::filesystem::path const& Path() const noexcept { return m_path; }

    private:
        static inline std::atomic_int s_counter{};
        std::filesystem::path m_path;
    };

    constexpr std::string_view IssuedOne = R"({"token":"local-1","expiresAt":4102444800000})";
    constexpr std::string_view IssuedTwo = R"({"token":"local-2","expiresAt":4102444800000})";

    // The reference's signInWithPassword(serverUrl, ...): the credentials go
    // to the server typed into the form, the session records that server, and
    // its refresh goes back to it.
    void TestPasswordSignInStaysOnItsServer()
    {
        AuthHttpServer builtIn{ [](std::string const&, std::string const&) { return Json(500, "{}"); } };
        AuthHttpServer own{ [](std::string const& path, std::string const& bearer)
        {
            if (path == "/auth/login") return Json(200, IssuedOne);
            if (path == "/auth/refresh" && bearer == "local-1") return Json(200, IssuedTwo);
            return Json(404, "{}");
        } };
        TemporaryFolder folder;
        auto const store = std::make_shared<HaloDesktop::Services::Auth::SessionStore>(folder.Path());
        HaloDesktop::Services::Auth::LocalAuthSession session{
            builtIn.BaseUrl(), std::make_shared<HaloDesktop::Api::HttpExecutor>(), store };

        auto const issued = session.LoginAsync(own.BaseUrl(), L"kenneth", L"correct horse").get();
        Require(issued.Token == L"local-1", "the self-hosted server's token was not taken");
        Require(session.ServerUrl() == own.BaseUrl(), "the session did not record its own server");
        auto const seen = own.Requests();
        Require(seen.size() == 1 && seen[0].Method == "POST" && seen[0].Path == "/auth/login"
                && seen[0].Body.find("correct horse") != std::string::npos,
            "the credentials did not reach the typed server");
        Require(builtIn.Requests().empty(), "the credentials reached the built-in server");

        auto const stored = store->LoadAsync().get();
        Require(stored && stored->Local && stored->Local->ServerUrl == own.BaseUrl()
                && stored->Local->Token == L"local-1",
            "the saved session did not keep its server");

        Require(session.RefreshAccessTokenAsync().get() == L"local-2", "the session did not refresh on its own server");
        Require(own.Requests().size() == 2 && own.Requests().back().Path == "/auth/refresh" && builtIn.Requests().empty(),
            "a self-hosted session's refresh went elsewhere");

        // A session saved before sessions recorded their server belongs to the
        // built-in one.
        HaloDesktop::Services::Auth::LocalAuthSession older{
            builtIn.BaseUrl(), std::make_shared<HaloDesktop::Api::HttpExecutor>(), store };
        older.Restore({ .Token = L"old", .ExpiresAt = 4102444800000LL, .ServerUrl = std::nullopt });
        Require(older.ServerUrl() == builtIn.BaseUrl(), "an older saved session was not the built-in server's");
    }

    // Never after a redirect: a server that answers the sign-in with a 307 to
    // another origin gets its refusal, and the other origin sees nothing.
    void TestPasswordNeverFollowsARedirect()
    {
        AuthHttpServer elsewhere{ [](std::string const&, std::string const&) { return Json(200, IssuedOne); } };
        auto const location = winrt::to_string(elsewhere.BaseUrl()) + "/auth/login";
        AuthHttpServer typed{ [location](std::string const&, std::string const&)
        {
            return CannedResponse{ 307, "", "text/plain", "Location: " + location + "\r\n" };
        } };
        TemporaryFolder folder;
        auto const store = std::make_shared<HaloDesktop::Services::Auth::SessionStore>(folder.Path());
        HaloDesktop::Services::Auth::LocalAuthSession session{
            typed.BaseUrl(), std::make_shared<HaloDesktop::Api::HttpExecutor>(), store };

        auto const failure = FailureOf([&session, &typed]()
        {
            static_cast<void>(session.LoginAsync(typed.BaseUrl(), L"kenneth", L"secret").get());
        });
        Require(failure && HaloDesktop::Api::ApiError::HttpStatus(*failure).value_or(0) == 307,
            "a redirected sign-in did not fail with its redirect");
        Require(elsewhere.Requests().empty(), "the password followed a redirect to another origin");
        Require(!session.ServerUrl() && !store->LoadAsync().get(), "a refused sign-in left a session behind");
    }
}

void RunApiClientAuthTest()
{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    try
    {
        TestRefreshedOnceAndRetried();
        TestRejectedRefreshEndsSession();
        TestUnauthorizedAfterRefreshKeepsSession();
        TestLaterRequestSucceeds();
        TestRefreshTransportFailureKeepsSession();
        TestAvatarAndPlanRefusal();
        TestRequestsFollowTheSessionServer();
        TestPasswordSignInStaysOnItsServer();
        TestPasswordNeverFollowsARedirect();
    }
    catch (...)
    {
        winrt::uninit_apartment();
        throw;
    }
    winrt::uninit_apartment();
}
