#pragma once

#include "Api/Dto.h"

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
}

namespace HaloDesktop::Api
{
    class HttpExecutor;

    // Thread-safe endpoint facade. Public discovery bypasses authentication;
    // protected endpoints draw a token and spend at most one refresh retry.
    class ApiClient final
    {
    public:
        ApiClient(
            winrt::hstring baseUrl,
            std::shared_ptr<HttpExecutor> executor,
            std::shared_ptr<::HaloDesktop::Services::Auth::ITokenProvider> tokenProvider);

        [[nodiscard]] winrt::hstring BaseUrl() const;
        [[nodiscard]] concurrency::task<Dto::HealthStatus> GetHealthAsync();
        [[nodiscard]] concurrency::task<Dto::AuthConfig> GetAuthConfigAsync();
        [[nodiscard]] concurrency::task<Dto::Me> GetMeAsync();
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
        [[nodiscard]] concurrency::task<Dto::MetaDetail> GetMetaAsync(winrt::hstring type,winrt::hstring metaId);
        [[nodiscard]] concurrency::task<std::vector<Dto::LibraryRow>> PutLibraryAsync(std::vector<Dto::LibraryRow> rows);
        [[nodiscard]] concurrency::task<Dto::StreamsPayload> GetStreamsAsync(winrt::hstring type,winrt::hstring videoId);

    private:
        [[nodiscard]] winrt::Windows::Foundation::Uri Endpoint(wchar_t const* path) const;
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
