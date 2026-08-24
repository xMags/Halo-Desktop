#include "pch.h"
#include "Api/ApiClient.h"
#include "Api/HttpExecutor.h"

#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>
#include <winrt/Windows.Web.Http.h>

namespace
{
    winrt::hstring NormalizeBaseUrl(winrt::hstring const& input)
    {
        std::wstring baseUrl{ input };
        while (!baseUrl.empty() && baseUrl.back() == L'/')
        {
            baseUrl.pop_back();
        }
        if (baseUrl.empty())
        {
            throw std::invalid_argument{ "The server base URL is empty." };
        }

        winrt::Windows::Foundation::Uri const uri{ baseUrl };
        auto const scheme = uri.SchemeName();
        if ((scheme != L"http" && scheme != L"https") || uri.Host().empty())
        {
            throw std::invalid_argument{ "The server base URL must be an absolute HTTP or HTTPS URL." };
        }
        return winrt::hstring{ baseUrl };
    }
}

namespace HaloDesktop::Api
{
    ApiClient::ApiClient(
        winrt::hstring baseUrl,
        std::shared_ptr<HttpExecutor> executor)
        : m_baseUrl(NormalizeBaseUrl(baseUrl)),
          m_executor(std::move(executor))
    {
        if (!m_executor)
        {
            throw std::invalid_argument{ "ApiClient requires an HTTP executor." };
        }
    }

    winrt::hstring ApiClient::BaseUrl() const
    {
        return m_baseUrl;
    }

    concurrency::task<Dto::HealthStatus> ApiClient::GetHealthAsync()
    {
        co_await winrt::resume_background();
        auto const started = std::chrono::steady_clock::now();
        auto const value = co_await m_executor->SendJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Get(),
            Endpoint(L"/health"));
        auto const elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);
        co_return Dto::HealthStatus{
            .Ok = Mappers::ParseHealth(value),
            .RoundTrip = elapsed,
        };
    }

    concurrency::task<Dto::AuthConfig> ApiClient::GetAuthConfigAsync()
    {
        co_await winrt::resume_background();
        auto const value = co_await m_executor->SendJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Get(),
            Endpoint(L"/auth/config"));
        co_return Mappers::ParseAuthConfig(value);
    }

    winrt::Windows::Foundation::Uri ApiClient::Endpoint(wchar_t const* path) const
    {
        if (!path || path[0] != L'/')
        {
            throw std::invalid_argument{ "API endpoint paths must begin with a slash." };
        }
        return winrt::Windows::Foundation::Uri{ m_baseUrl + path };
    }
}
