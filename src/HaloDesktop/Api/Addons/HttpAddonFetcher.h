#pragma once

#include "Api/Addons/AddonFetcher.h"

#include <cstddef>
#include <cstdint>
#include <vector>
#include <winrt/Windows.Web.Http.Filters.h>
#include <winrt/Windows.Web.Http.h>

namespace HaloDesktop::Api::Addons
{
    // Asks third-party addons directly, for Halo used without an account,
    // where the server used to ask them. It has its own HTTP client so that
    // nothing of the account's traffic (cookies, cached credentials) is ever
    // shared with an addon host:
    //
    // - GET only, http and https only; every redirect is followed by hand
    //   (at most MaximumRedirects) and refused unless it stays http or https.
    // - No cookies, no credentials, no Authorization header, no prompts, no
    //   HTTP cache.
    // - One budget for the whole request, redirects and body included; when
    //   it runs out the request in flight is cancelled and the response
    //   closed, and the call fails as Timeout.
    // - The body is read through the app's bounded reader
    //   (MaximumJsonResponseBytes, the bound the server's own answers have),
    //   and only for a 2xx answer.
    //
    // Thread-safe; every call leaves the caller's apartment first.
    class HttpAddonFetcher final : public IAddonFetcher
    {
    public:
        // reqwest's default, which the reference desktop's fetch uses.
        static constexpr std::size_t MaximumRedirects = 10;

        HttpAddonFetcher();

        [[nodiscard]] concurrency::task<AddonFetchResponse> GetAsync(
            std::wstring url,
            std::chrono::milliseconds budget,
            concurrency::cancellation_token cancellation) override;

        // A file an addon links to (a subtitle), fetched directly under the
        // same rules as GetAsync, for Halo without an account, where the
        // server's addon proxy is not there. The body is returned as raw bytes
        // and refused (InvalidResponse) once it passes maximumBytes, declared
        // or not; any answer other than 2xx fails as UpstreamHttp.
        [[nodiscard]] concurrency::task<std::vector<std::uint8_t>> GetFileAsync(
            std::wstring url,
            std::chrono::milliseconds budget,
            std::size_t maximumBytes,
            concurrency::cancellation_token cancellation);

    private:
        winrt::Windows::Web::Http::Filters::HttpBaseProtocolFilter m_filter;
        winrt::Windows::Web::Http::HttpClient m_client;
    };
}
