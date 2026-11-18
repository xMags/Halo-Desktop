#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <pplawait.h>
#include <ppltasks.h>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>
#include <winrt/Windows.Data.Json.h>
#include <winrt/base.h>

namespace HaloDesktop::Api
{
    class HttpExecutor;
}

namespace HaloDesktop::Services::Auth
{
    // Extension point for sign-in methods a local build adds.
    //
    // A server's /auth/config names how it signs people in. This app knows two
    // modes itself: "oidc" and "local". Any other mode needs a sign-in add-on
    // for it, or the sign-in screen says that this build cannot sign in to
    // that server. A public build carries no add-ons.
    //
    // How a local build adds one:
    //
    //   1. Create src/HaloDesktop/Local/SignInAddons.cpp. That folder is
    //      git-ignored, so the file never reaches the public repository.
    //   2. Define in it, in namespace HaloDesktop::Services::Auth:
    //
    //        std::vector<std::shared_ptr<ISignInAddon>> CreateSignInAddons(
    //            SignInAddonContext const& context);
    //
    //      returning one ISignInAddon per mode the build adds.
    //   3. Build as usual. HaloDesktop.vcxproj compiles that file when it
    //      exists and Services/Auth/NoSignInAddons.cpp (whose definition
    //      returns nothing) when it does not; both item entries carry a
    //      Condition on Exists('$(MSBuildThisFileDirectory)Local\SignInAddons.cpp'),
    //      so exactly one definition is linked and a public checkout builds
    //      with no private file and no edits.
    //
    // The file may include "pch.h" like any other source of the app.
    //
    // An add-on owns its whole session: persistence, the token the API client
    // sends, what a refused refresh means, and sign-out. It keeps the app's
    // rule that only a definitive rejection ends a session: a network failure
    // throws and never ends one. Its sessions run under the session kind
    // `addon:<mode>` and always talk to the built-in server.

    // What /auth/config answered for a mode the app does not know itself. Raw
    // is the whole answer and is untrusted: the add-on validates every field it
    // reads.
    struct AddonAuthConfig final
    {
        std::wstring Mode;
        winrt::Windows::Data::Json::JsonObject Raw{ nullptr };
    };

    // Hears the sign-in page's address once the browser has it, so the sign-in
    // screen can offer to open it again. May be called on any thread.
    using SignInOpenedHandler = std::function<void(winrt::hstring const& authUrl)>;

    // Every member may be called on any thread, concurrently with the others;
    // an add-on synchronises itself. Long work never blocks the caller: it is
    // the returned task's.
    class ISignInAddon
    {
    public:
        virtual ~ISignInAddon() = default;

        // The /auth/config mode this add-on signs in for. Not "oidc" or
        // "local", which the app always handles itself.
        [[nodiscard]] virtual std::wstring Mode() const = 0;
        // The sign-in button's text, such as "Continue with Example". A label
        // starting with the word "Continue" reads "Sign in ... instead" on the
        // username and password form's way back.
        [[nodiscard]] virtual std::wstring ButtonLabel() const = 0;

        // Runs the browser sign-in for the server at serverUrl, whose
        // /auth/config answered config, and persists the session it gets.
        // onOpened hears the sign-in page's address once the browser has it.
        // The cancellation token stops a wait the person abandoned; the task
        // then fails. Failures throw SignInError (Services/Auth/
        // SignInFailurePolicy.h): BrowserSignInFailure for a cancelled,
        // declined or expired request, SignInRefused (with the OAuth error code
        // when there is one) for a provider's refusal, so the screen shows its
        // reason. Anything else thrown reads as "can't reach Halo" with its
        // message as the technical detail. Services/Auth/BrowserSignInStep.h
        // runs the browser half (loopback listener, callback, cancel) the same
        // way the OIDC sign-in does.
        [[nodiscard]] virtual concurrency::task<void> SignInAsync(
            winrt::hstring serverUrl,
            AddonAuthConfig config,
            SignInOpenedHandler onOpened,
            std::stop_token cancellation) = 0;

        // Loads a persisted session into memory; true when there is one.
        [[nodiscard]] virtual concurrency::task<bool> RestoreAsync() = 0;
        // Forgets the session on this PC only.
        [[nodiscard]] virtual concurrency::task<void> ClearAsync() = 0;
        // Ends the session with the provider (best effort) and forgets it.
        [[nodiscard]] virtual concurrency::task<void> SignOutAsync() = 0;
        // The token the API client sends, refreshed as the add-on sees fit;
        // null without a session.
        [[nodiscard]] virtual concurrency::task<std::optional<winrt::hstring>> AccessTokenAsync() = 0;
        // The API client's hook after a 401: a token to retry with, or null
        // once the session is definitively over. A network failure throws.
        [[nodiscard]] virtual concurrency::task<std::optional<winrt::hstring>> RefreshAccessTokenAsync() = 0;

        // An opaque local partition for downloads, kept with the session. Not
        // an authentication claim.
        [[nodiscard]] virtual std::optional<std::wstring> DownloadOwner() const = 0;
        virtual void SetDownloadOwner(std::wstring downloadOwner) = 0;
    };

    // What the app hands the add-ons it creates.
    struct SignInAddonContext final
    {
        // The app's HTTP client, for the add-on's own requests.
        std::shared_ptr<::HaloDesktop::Api::HttpExecutor> Http;
        // This PC's private app data folder, where an add-on may persist its
        // session. Protect anything secret written there (Security/Dpapi.h).
        std::filesystem::path LocalState;
    };

    // The build's sign-in add-ons. Called once at start, on the UI thread,
    // before any session is restored. See the contract above.
    [[nodiscard]] std::vector<std::shared_ptr<ISignInAddon>> CreateSignInAddons(
        SignInAddonContext const& context);
}
