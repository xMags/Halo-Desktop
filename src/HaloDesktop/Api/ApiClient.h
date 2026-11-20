#pragma once

#include "Api/Dto.h"
#include "Api/HaloBackend.h"
#include "Api/OpenSubtitlesHash.h"
#include "Api/PeopleDirectory.h"

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
    //
    // The IHaloBackend part is what Halo used with an account asks; the rest
    // is account-only.
    class ApiClient final : public IHaloBackend, public IPeopleDirectory
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
        [[nodiscard]] concurrency::task<Dto::AddonsPayload> GetAddonsAsync() override;
        // The caller's own list: PutAddonsAsync(transportUrls, false).
        [[nodiscard]] concurrency::task<void> PutAddonsAsync(std::vector<winrt::hstring> transportUrls) override;
        [[nodiscard]] concurrency::task<void> PutAddonsAsync(
            std::vector<winrt::hstring> transportUrls,
            bool global);
        // The caller's own addon: PatchAddonAsync(addonId, false, hideCatalogs).
        [[nodiscard]] concurrency::task<void> PatchAddonAsync(winrt::hstring addonId, bool hideCatalogs) override;
        [[nodiscard]] concurrency::task<void> PatchAddonAsync(
            winrt::hstring addonId,
            bool global,
            bool hideCatalogs);
        [[nodiscard]] concurrency::task<Dto::SettingsPayload> GetSettingsAsync() override;
        [[nodiscard]] concurrency::task<Dto::SettingsPayload> PutSettingsAsync(
            winrt::Windows::Data::Json::JsonObject value,
            std::int64_t updatedAt) override;
        [[nodiscard]] concurrency::task<std::vector<Dto::MetaPreview>> GetCatalogAsync(
            winrt::hstring addonId,
            winrt::hstring type,
            winrt::hstring catalogId,
            std::vector<std::pair<winrt::hstring, winrt::hstring>> extras = {}) override;
        [[nodiscard]] concurrency::task<std::vector<Dto::LibraryRow>> GetLibraryAsync() override;
        [[nodiscard]] concurrency::task<std::vector<Dto::WatchEntry>> GetWatchStateAsync() override;
        [[nodiscard]] concurrency::task<std::vector<Dto::WatchEntry>> PutWatchStateAsync(std::vector<Dto::WatchEntry> rows) override;
        [[nodiscard]] concurrency::task<Dto::MetaDetail> GetMetaAsync(winrt::hstring type,winrt::hstring metaId) override;
        [[nodiscard]] concurrency::task<std::vector<Dto::LibraryRow>> PutLibraryAsync(std::vector<Dto::LibraryRow> rows) override;
        [[nodiscard]] concurrency::task<Dto::StreamsPayload> GetStreamsAsync(winrt::hstring type,winrt::hstring videoId) override;
        [[nodiscard]] concurrency::task<Dto::NextEpisodePayload> GetNextEpisodeAsync(
            winrt::hstring type,
            winrt::hstring metaId,
            winrt::hstring videoId,
            winrt::hstring addonId,
            winrt::hstring bingeGroup) override;
        [[nodiscard]] concurrency::task<Dto::SubtitlesPayload> GetSubtitlesAsync(winrt::hstring type,winrt::hstring videoId,std::optional<winrt::hstring> videoHash,std::optional<std::uint64_t> videoSize,std::optional<winrt::hstring> filename) override;
        [[nodiscard]] concurrency::task<winrt::Windows::Web::Http::HttpResponseMessage> OpenAddonProxyAsync(winrt::hstring targetUrl);
        [[nodiscard]] concurrency::task<AuthenticatedDownloadRequest> BuildAddonProxyDownloadRequestAsync(
            winrt::hstring targetUrl);
        [[nodiscard]] concurrency::task<VideoHashResult> ComputeVideoHashAsync(
            winrt::hstring streamUrl,
            Security::ProtectedHttpHeaders headers = {});

        // What this server can do beyond the shared contract. A server without
        // the endpoint answers 404, which throws like any other failure.
        [[nodiscard]] concurrency::task<Dto::Capabilities> GetCapabilitiesAsync();
        // People whose name matches `query` (trimmed here; 2 to 120 characters,
        // else std::invalid_argument before anything is sent).
        [[nodiscard]] concurrency::task<Dto::PeopleSearch> SearchPeopleAsync(winrt::hstring query) override;
        // A person and their films and series. 404: no such person.
        [[nodiscard]] concurrency::task<Dto::PersonDetail> GetPersonAsync(std::int64_t tmdbId) override;
        // What a pasted id names. `source` is `tvdb` (digits) or `imdb`
        // (`tt` or `nm` and digits); anything else is refused before sending.
        // 404: nothing known by that id.
        [[nodiscard]] concurrency::task<Dto::IdLookup> LookupAsync(winrt::hstring source, winrt::hstring id) override;

        // A pasted magnet, sent as its info hash (40 lower-case hex) and an
        // optional display name; never the magnet link or its trackers. Each
        // file's play link is rebuilt on this session's server (ParseMagnet),
        // and is meant to be opened bare: no token is ever attached to it.
        // 404: magnets are not available to this account.
        [[nodiscard]] concurrency::task<Dto::MagnetRequest> AddMagnetAsync(winrt::hstring infoHash, winrt::hstring name);
        // The request again, to follow a download. 404: no such request.
        [[nodiscard]] concurrency::task<Dto::MagnetRequest> GetMagnetAsync(winrt::hstring id);
        // Forgets the request; the server removes the torrent it added for it.
        [[nodiscard]] concurrency::task<void> ForgetMagnetAsync(winrt::hstring id);

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
