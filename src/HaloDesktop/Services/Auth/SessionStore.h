#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
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

    // Thread-safe. Storage is one current-user DPAPI file in LocalFolder.
    class SessionStore final
    {
    public:
        SessionStore();

        [[nodiscard]] concurrency::task<std::optional<StoredSession>> LoadAsync();
        [[nodiscard]] concurrency::task<void> SaveAsync(StoredSession session);
        [[nodiscard]] concurrency::task<void> ClearAsync();

    private:
        std::filesystem::path m_path;
    };
}
