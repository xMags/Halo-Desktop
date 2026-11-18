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
    // network or body work. The no-redirect client is reserved for requests
    // that carry a credential in their body (IdP form posts, a username and
    // password sign-in and its token refresh), where following a redirect
    // could hand the credential to another origin.
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

        // SendJsonAsync without following redirects: a 3xx answer fails with
        // its status like any other refusal.
        [[nodiscard]] winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Data::Json::IJsonValue>
            SendJsonWithoutRedirectAsync(
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
        [[nodiscard]] static winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Data::Json::IJsonValue>
            SendJsonWithAsync(
                winrt::Windows::Web::Http::HttpClient client,
                winrt::Windows::Web::Http::HttpMethod method,
                winrt::Windows::Foundation::Uri uri,
                std::optional<winrt::hstring> body,
                HttpHeaders headers);

        winrt::Windows::Web::Http::Filters::HttpBaseProtocolFilter m_apiFilter;
        winrt::Windows::Web::Http::Filters::HttpBaseProtocolFilter m_noRedirectFilter;
        winrt::Windows::Web::Http::HttpClient m_apiClient;
        winrt::Windows::Web::Http::HttpClient m_noRedirectClient;
    };
}
