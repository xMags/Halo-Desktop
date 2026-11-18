#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <mutex>
#include <pplawait.h>
#include <ppltasks.h>
#include <winrt/base.h>

namespace HaloDesktop::Services::Auth
{
    enum class StoredSessionKind
    {
        Local,
        Oidc,
    };

    struct StoredLocalSession final
    {
        winrt::hstring Token;
        std::int64_t ExpiresAt{};
        // The server the session was opened on, which issues and refreshes its
        // token and answers every request made with it. Absent in a file
        // written before sessions recorded it; those were the built-in
        // server's.
        std::optional<winrt::hstring> ServerUrl;
    };

    struct StoredOidcSession final
    {
        winrt::hstring ClientId;
        winrt::hstring TokenEndpoint;
        std::optional<winrt::hstring> RevocationEndpoint;
        std::optional<winrt::hstring> EndSessionEndpoint;
        winrt::hstring AccessToken;
        std::optional<winrt::hstring> RefreshToken;
        std::optional<winrt::hstring> IdToken;
        std::int64_t ExpiresAt{};
    };

    struct StoredSession final
    {
        StoredSessionKind Kind{ StoredSessionKind::Local };
        std::optional<StoredLocalSession> Local;
        std::optional<StoredOidcSession> Oidc;
    };

    struct StoredIdentity final
    {
        winrt::hstring UserId;
        winrt::hstring Username;
        // What the last /auth/me said about the account, so a launch can show it
        // before the server answers again. A file written before these were kept,
        // or holding a value that does not parse, reads them as unknown.
        std::optional<bool> IsAdmin;
        std::optional<winrt::hstring> Plan;
        std::optional<winrt::hstring> AvatarVersion;
    };

    // Thread-safe. Storage is one current-user DPAPI file in LocalFolder.
    class SessionStore final
    {
    public:
        explicit SessionStore(std::filesystem::path localState);

        [[nodiscard]] concurrency::task<std::optional<StoredSession>> LoadAsync();
        [[nodiscard]] concurrency::task<void> SaveAsync(StoredSession session);
        [[nodiscard]] concurrency::task<void> ClearAsync();
        [[nodiscard]] concurrency::task<std::optional<StoredIdentity>> LoadIdentityAsync();
        [[nodiscard]] concurrency::task<void> SaveIdentityAsync(
            StoredIdentity identity,
            std::uint64_t generation);
        [[nodiscard]] concurrency::task<void> ClearIdentityAsync(std::uint64_t generation);

    private:
        std::filesystem::path m_path;
        std::filesystem::path m_identityPath;
        std::mutex m_identityQueueMutex;
        concurrency::task<void> m_identityTail{ concurrency::task_from_result() };
        std::uint64_t m_identityGeneration{};
    };
}
