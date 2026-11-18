#pragma once

#include <string>
#include <string_view>

namespace HaloDesktop::Services::Auth
{
    // The tab the browser lands on after the loopback redirect, in the sign-in
    // screen's look, as the reference client's `callback_page.html` and
    // `oauth.rs` draw it. Pure.
    //
    // A provider that refused or failed sends an `error` parameter instead of
    // a code; that tab says sign-in did not finish rather than claiming a
    // sign-in the app is about to report as declined. Every value on the page
    // is a constant: nothing from the request reaches it.
    [[nodiscard]] bool CallbackReportsError(std::string_view requestPath) noexcept;

    // The page as UTF-8 HTML for the callback at requestPath.
    [[nodiscard]] std::string LoopbackLandingPage(std::string_view requestPath);

    // The whole HTTP response for it, never cached by the browser.
    [[nodiscard]] std::string LoopbackLandingResponse(std::string_view requestPath);
}
