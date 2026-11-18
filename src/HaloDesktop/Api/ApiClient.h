#pragma once

#include "Api/Dto.h"
#include "Api/OpenSubtitlesHash.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>
#include <pplawait.h>
#include <ppltasks.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Web.Http.h>

namespace HaloDesktop::Services::Auth
{
    class ITokenProvider;
    struct SessionTarget;
}

namespace HaloDesktop::Api
{
    class HttpExecutor;

    struct AuthenticatedDownloadRequest final
    {
        winrt::hstring Url;
        std::vector<std::pair<winrt::hstring, winrt::hstring>> Headers;
    };

    // Thread-safe endpoint facade. Public discovery bypasses authentication;
    // protected endpoints draw a token and spend at most one refresh retry.
    // Every request for a session goes to that session's own server (the
    // token provider's CurrentTarget), which is the built-in base URL unless a
    // username and password session was opened on a self-hosted server. A
    // request whose target changes while it is under way is cancelled, so one
    // session's token never reaches another session's server.
    class ApiClient final
    {
    public:
        static constexpr std::size_t MaximumAvatarBytes = 4u * 1024u * 1024u;

        ApiClient(
            winrt::hstring baseUrl,
            std::shared_ptr<HttpExecutor> executor,
            std::shared_ptr<::HaloDesktop::Services::Auth::ITokenProvider> tokenProvider);

        // The built-in server this build signs in to.
        [[nodiscard]] winrt::hstring BaseUrl() const;
        [[nodiscard]] concurrency::task<Dto::HealthStatus> GetHealthAsync();
        // The built-in server's sign-in settings.
        [[nodiscard]] concurrency::task<Dto::AuthConfig> GetAuthConfigAsync();
        // Another server's, for the self-hosted sign-in form. Unauthenticated.
        [[nodiscard]] concurrency::task<Dto::AuthConfig> GetAuthConfigAtAsync(winrt::hstring serverUrl);
        [[nodiscard]] concurrency::task<Dto::Me> GetMeAsync();
        // The account's profile picture as the server sent it (an image file),
        // or empty when it has none. Fetch it again only when
        // Dto::Me::AvatarVersion changes. Larger than MaximumAvatarBytes fails.
        [[nodiscard]] concurrency::task<std::optional<std::vector<std::uint8_t>>> GetAvatarAsync();
        [[nodiscard]] concurrency::task<Dto::AddonsPayload> GetAddonsAsync();
        [[nodiscard]] concurrency::task<void> PutAddonsAsync(
            std::vector<winrt::hstring> transportUrls,
            bool global);
        [[nodiscard]] concurrency::task<void> PatchAddonAsync(
            winrt::hstring addonId,
            bool global,
            bool hideCatalogs);
        [[nodiscard]] concurrency::task<Dto::SettingsPayload> GetSettingsAsync();
        [[nodiscard]] concurrency::task<Dto::SettingsPayload> PutSettingsAsync(
            winrt::Windows::Data::Json::JsonObject value,
            std::int64_t updatedAt);
        [[nodiscard]] concurrency::task<std::vector<Dto::MetaPreview>> GetCatalogAsync(
            winrt::hstring addonId,
            winrt::hstring type,
            winrt::hstring catalogId,
            std::vector<std::pair<winrt::hstring, winrt::hstring>> extras = {});
        [[nodiscard]] concurrency::task<std::vector<Dto::LibraryRow>> GetLibraryAsync();
        [[nodiscard]] concurrency::task<std::vector<Dto::WatchEntry>> GetWatchStateAsync();
        [[nodiscard]] concurrency::task<std::vector<Dto::WatchEntry>> PutWatchStateAsync(std::vector<Dto::WatchEntry> rows);
        [[nodiscard]] concurrency::task<Dto::MetaDetail> GetMetaAsync(winrt::hstring type,winrt::hstring metaId);
        [[nodiscard]] concurrency::task<std::vector<Dto::LibraryRow>> PutLibraryAsync(std::vector<Dto::LibraryRow> rows);
        [[nodiscard]] concurrency::task<Dto::StreamsPayload> GetStreamsAsync(winrt::hstring type,winrt::hstring videoId);
        [[nodiscard]] concurrency::task<Dto::NextEpisodePayload> GetNextEpisodeAsync(
            winrt::hstring type,
            winrt::hstring metaId,
            winrt::hstring videoId,
            winrt::hstring addonId,
            winrt::hstring bingeGroup);
        [[nodiscard]] concurrency::task<Dto::SubtitlesPayload> GetSubtitlesAsync(winrt::hstring type,winrt::hstring videoId,std::optional<winrt::hstring> videoHash,std::optional<std::uint64_t> videoSize,std::optional<winrt::hstring> filename);
        [[nodiscard]] concurrency::task<winrt::Windows::Web::Http::HttpResponseMessage> OpenAddonProxyAsync(winrt::hstring targetUrl);
        [[nodiscard]] concurrency::task<AuthenticatedDownloadRequest> BuildAddonProxyDownloadRequestAsync(
            winrt::hstring targetUrl);
        [[nodiscard]] concurrency::task<VideoHashResult> ComputeVideoHashAsync(
            winrt::hstring streamUrl,
            Security::ProtectedHttpHeaders headers = {});

    private:
        [[nodiscard]] static winrt::Windows::Foundation::Uri Endpoint(winrt::hstring const& server, wchar_t const* path);
        [[nodiscard]] winrt::hstring ServerFor(::HaloDesktop::Services::Auth::SessionTarget const& target) const;
        void RequireSameTarget(::HaloDesktop::Services::Auth::SessionTarget const& target) const;
        [[nodiscard]] concurrency::task<winrt::Windows::Data::Json::IJsonValue> SendAuthenticatedJsonAsync(
            winrt::Windows::Web::Http::HttpMethod method,
            wchar_t const* path,
            std::optional<winrt::hstring> body = std::nullopt);
        [[noreturn]] static void ThrowSessionRejected();

        winrt::hstring m_baseUrl;
        std::shared_ptr<HttpExecutor> m_executor;
        std::shared_ptr<::HaloDesktop::Services::Auth::ITokenProvider> m_tokenProvider;
    };
}
