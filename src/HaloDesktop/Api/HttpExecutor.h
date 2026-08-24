#pragma once

#include <optional>
#include <utility>
#include <vector>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Web.Http.Filters.h>
#include <winrt/Windows.Web.Http.h>

namespace HaloDesktop::Api
{
    using HttpHeader = std::pair<winrt::hstring, winrt::hstring>;
    using HttpHeaders = std::vector<HttpHeader>;

    // Thread-safe. Every operation leaves the caller's apartment before doing
    // network or body work. The no-redirect client is reserved for IdP form
    // posts, where a redirected token endpoint changes the request semantics.
    class HttpExecutor final
    {
    public:
        HttpExecutor();

        [[nodiscard]] winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Data::Json::IJsonValue>
            SendJsonAsync(
                winrt::Windows::Web::Http::HttpMethod method,
                winrt::Windows::Foundation::Uri uri,
                std::optional<winrt::hstring> body = std::nullopt,
                HttpHeaders headers = {});

        [[nodiscard]] winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Web::Http::HttpResponseMessage>
            SendForStreamAsync(winrt::Windows::Web::Http::HttpRequestMessage request);

        [[nodiscard]] winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Web::Http::HttpResponseMessage>
            SendFormWithoutRedirectAsync(
                winrt::Windows::Foundation::Uri uri,
                winrt::hstring body);

    private:
        winrt::Windows::Web::Http::Filters::HttpBaseProtocolFilter m_apiFilter;
        winrt::Windows::Web::Http::Filters::HttpBaseProtocolFilter m_noRedirectFilter;
        winrt::Windows::Web::Http::HttpClient m_apiClient;
        winrt::Windows::Web::Http::HttpClient m_noRedirectClient;
    };
}
