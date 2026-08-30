#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <winrt/Windows.Foundation.h>

namespace HaloDesktop::Security
{
    // Every Halo transport that carries protected headers follows redirects
    // itself rather than letting its HTTP stack do it, because the stacks keep
    // custom headers across origins. They share these rules so one transport
    // cannot quietly become more permissive than another.
    inline constexpr int MaximumProtectedRedirects = 5;
    inline constexpr std::size_t MaximumLocationLength = 32768;

    struct RedirectTarget final
    {
        std::wstring Url;
        bool SameOrigin{};
    };

    [[nodiscard]] bool IsRedirectStatus(std::uint32_t status) noexcept;

    [[nodiscard]] bool SameHttpOrigin(
        winrt::Windows::Foundation::Uri const& left,
        winrt::Windows::Foundation::Uri const& right) noexcept;

    // Resolves one Location header against the URL that produced it. Returns
    // nullopt when the hop must not be followed at all: too many hops, an
    // unusable Location, a scheme that is not http(s), or a step down out of
    // https. SameOrigin is false when the hop leaves the current origin, which
    // is the caller's signal to stop forwarding protected headers.
    [[nodiscard]] std::optional<RedirectTarget> NextRedirectTarget(
        std::wstring const& currentUrl,
        std::wstring const& location,
        int hopsAlreadyFollowed) noexcept;
}
