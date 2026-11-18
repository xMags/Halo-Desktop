#pragma once

#include "Services/Auth/SignInAddon.h"
#include "Services/Auth/SignInFailurePolicy.h"

#include <exception>
#include <optional>
#include <pplawait.h>
#include <ppltasks.h>
#include <stop_token>
#include <winrt/base.h>

namespace HaloDesktop::Services::Auth
{
    // The redirect URI every browser sign-in registers.
    inline constexpr wchar_t LoopbackRedirectUri[] = L"http://127.0.0.1:17871/callback";

    // The request the browser was redirected back with.
    struct BrowserCallback final
    {
        // The callback's path and query, as the listener received it.
        winrt::hstring Path;

        // A query parameter, decoded; null when absent.
        [[nodiscard]] std::optional<winrt::hstring> Value(wchar_t const* name) const;
    };

    // The browser half of an RFC 8252 sign-in, shared by the OIDC sign-in and
    // the sign-in add-ons: binds the loopback listener, opens authUrl in the
    // default browser, tells onOpened, and completes with the callback once
    // the browser is redirected back. The listener is bound before the browser
    // opens, so the redirect cannot race the bind.
    //
    // Throws SignInError: cancelled when the token is stopped, expired when the
    // listener times out, declined when the provider sent
    // `error=access_denied`. A listener that could not start or a browser that
    // would not open throws SignInFailed with what went wrong. Any other
    // provider error is left in the callback for the caller to read.
    //
    // With expectedState (the `state` the caller put in authUrl), only a
    // callback carrying that state completes the sign-in, a provider error
    // that carries it included; any other request to the port is answered 404
    // and the wait goes on. Without it, the first callback completes it and
    // the caller compares the state itself.
    [[nodiscard]] concurrency::task<BrowserCallback> SignInInBrowserAsync(
        winrt::hstring authUrl,
        std::stop_token cancellation,
        SignInOpenedHandler onOpened,
        std::optional<winrt::hstring> expectedState = std::nullopt);

    // What a failed sign-in step threw, for ClassifySignInFailure: a
    // SignInError as it is, anything else by its message.
    [[nodiscard]] SignInErrorInfo DescribeSignInFailure(std::exception_ptr const& failure);
}
