#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace HaloDesktop::Services::Auth
{
    // What the sign-in form's "Server address" field reads as, mirroring the
    // reference client's `parseServerAddress` (apps/desktop/src/serverUrl.ts):
    // https unless the server is on this PC, and only the address itself. A
    // bare host gets https in front. Either Address holds the address without
    // a trailing slash, or Error holds the sentence the form shows.
    struct ServerAddressResult final
    {
        std::optional<std::wstring> Address;
        std::wstring Error;
    };

    // The field's limit, which the form also applies as it is typed.
    inline constexpr std::size_t MaximumServerAddressLength = 512;

    inline constexpr std::wstring_view ServerAddressEmptyError = L"Enter your server’s address.";
    inline constexpr std::wstring_view ServerAddressInvalidError = L"That isn’t a server address.";
    inline constexpr std::wstring_view ServerAddressInsecureError =
        L"Use an https address. Plain http only works for a server on this PC.";
    inline constexpr std::wstring_view ServerAddressExtraError = L"Enter only the server’s address.";

    // Pure. Differences from a browser's URL parser are all refusals: a host
    // outside ASCII letters, digits, dots, hyphens and underscores, a
    // backslash, a space or control character, or any `?` or `#` (even an
    // empty one) is refused rather than rewritten.
    [[nodiscard]] ServerAddressResult ParseServerAddress(std::wstring_view raw);

    // 127.0.0.1, localhost and [::1], the only hosts plain http may reach.
    [[nodiscard]] bool IsLoopbackHost(std::wstring_view host) noexcept;

    // Whether sessions saved on this PC may be kept for the built-in server
    // this build signs in to, given the built-in server recorded when they
    // were saved (the reference's `halo.serverUrl`). Both are compared
    // without trailing slashes.
    enum class SessionServerCheck
    {
        // Recorded for this build's server: keep everything.
        Matches,
        // Nothing recorded, which is every install made before the record
        // existed: kept, and the record is written now.
        Unrecorded,
        // Recorded for another server: browser and add-on sessions are wiped
        // on this PC only, and a username and password session is kept only
        // when it names its own server.
        Different,
    };

    [[nodiscard]] SessionServerCheck CheckSessionServer(
        std::optional<std::wstring_view> recorded,
        std::wstring_view builtIn);
}
