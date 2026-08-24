#pragma once

#include "Api/Dto.h"

#include <memory>
#include <pplawait.h>
#include <ppltasks.h>
#include <winrt/Windows.Foundation.h>

namespace HaloDesktop::Api
{
    class HttpExecutor;

    // Thread-safe endpoint facade. Authentication and the one-refresh retry
    // policy are added when sessions become real in the next milestone.
    class ApiClient final
    {
    public:
        ApiClient(
            winrt::hstring baseUrl,
            std::shared_ptr<HttpExecutor> executor);

        [[nodiscard]] winrt::hstring BaseUrl() const;
        [[nodiscard]] concurrency::task<Dto::HealthStatus> GetHealthAsync();
        [[nodiscard]] concurrency::task<Dto::AuthConfig> GetAuthConfigAsync();

    private:
        [[nodiscard]] winrt::Windows::Foundation::Uri Endpoint(wchar_t const* path) const;

        winrt::hstring m_baseUrl;
        std::shared_ptr<HttpExecutor> m_executor;
    };
}
