#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <pplawait.h>
#include <ppltasks.h>
#include <winrt/base.h>

namespace HaloDesktop::Api::Addons
{
    // Why one addon request failed, in the categories the reference reports
    // (`addon/normalize.ts`, classifyAddonFailure).
    enum class AddonFailureKind
    {
        // The request's own budget ran out.
        Timeout,
        // The addon answered with an HTTP error.
        UpstreamHttp,
        // The request was refused for safety (a scheme other than http or
        // https, before or after a redirect).
        BlockedTarget,
        // The answer was not the JSON shape asked for, or was too large.
        InvalidResponse,
        // Anything else: no connection, a reset, too many redirects.
        Unavailable,
    };

    struct AddonFailure final
    {
        AddonFailureKind Kind{ AddonFailureKind::Unavailable };
        // UpstreamHttp only.
        std::optional<std::uint16_t> Status;
    };

    // Thrown by a fetcher and the resolvers for one failed addon request.
    class AddonRequestFailure final : public std::runtime_error
    {
    public:
        explicit AddonRequestFailure(AddonFailure failure)
            : std::runtime_error{ "An addon request failed." },
              m_failure(failure)
        {
        }

        [[nodiscard]] AddonFailure Failure() const noexcept
        {
            return m_failure;
        }

    private:
        AddonFailure m_failure;
    };

    struct AddonFetchResponse final
    {
        std::uint16_t Status{};
        // The body, read only for a 2xx answer (UTF-8 decoded); empty otherwise.
        winrt::hstring Body;
    };

    // One GET to a third-party addon. Implementations follow these rules:
    // http and https only, before and after every redirect; no cookies,
    // credentials or Authorization header; the whole request (connection,
    // redirects and body) within `budget`, after which it is really
    // cancelled and fails as Timeout; a bounded body.
    //
    // Fails with AddonRequestFailure, or winrt::hresult_canceled when
    // `cancellation` is cancelled. Thread-safe.
    class IAddonFetcher
    {
    public:
        virtual ~IAddonFetcher() = default;

        [[nodiscard]] virtual concurrency::task<AddonFetchResponse> GetAsync(
            std::wstring url,
            std::chrono::milliseconds budget,
            concurrency::cancellation_token cancellation) = 0;

    protected:
        IAddonFetcher() = default;
        IAddonFetcher(IAddonFetcher const&) = default;
        IAddonFetcher& operator=(IAddonFetcher const&) = default;
    };
}
