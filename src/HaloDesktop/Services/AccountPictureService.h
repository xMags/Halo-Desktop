#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <pplawait.h>
#include <ppltasks.h>
#include <vector>
#include <winrt/base.h>

namespace HaloDesktop::Api
{
    class ApiClient;
}

namespace HaloDesktop::Services
{
    // An image file exactly as the server sent it. Shared, never copied, and
    // never written to disk.
    using AccountPictureBytes = std::shared_ptr<std::vector<std::uint8_t> const>;

    // The signed-in account's profile picture. It is fetched only for a picture
    // fingerprint (Dto::Me::AvatarVersion) and kept in memory for that
    // fingerprint alone, so it is asked for again only after the picture changes.
    // A server without a picture (404) is remembered the same way; a failed fetch
    // is not, so the next request for that fingerprint tries again.
    //
    // UI-thread-only. Account-scoped: OnAccountChanged drops the cache, and a fetch
    // that began under the previous account resolves to no picture instead of to
    // that account's image.
    class AccountPictureService final
    {
    public:
        explicit AccountPictureService(std::shared_ptr<Api::ApiClient> apiClient);

        // Null when the fingerprint is empty, the account has no picture, or the
        // fetch failed. Never throws. Requests for the fingerprint already being
        // fetched share that one request.
        [[nodiscard]] concurrency::task<AccountPictureBytes> PictureAsync(winrt::hstring version);

        void OnAccountChanged() noexcept;

    private:
        [[nodiscard]] concurrency::task<AccountPictureBytes> FetchAsync(
            winrt::hstring version,
            std::uint64_t generation);

        std::shared_ptr<Api::ApiClient> m_apiClient;
        std::uint64_t m_generation{};
        std::optional<winrt::hstring> m_cachedVersion;
        AccountPictureBytes m_cached;
        std::optional<winrt::hstring> m_pendingVersion;
        std::optional<concurrency::task<AccountPictureBytes>> m_pending;
    };
}
