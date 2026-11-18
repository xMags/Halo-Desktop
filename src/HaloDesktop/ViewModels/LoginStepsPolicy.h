#pragma once

#include "Services/Auth/SignInFailurePolicy.h"

#include <optional>
#include <string>
#include <string_view>

namespace HaloDesktop::ViewModels
{
    // The sign-in screen's steps and copy, mirroring the reference client's
    // `auth/Login.tsx`. Pure: the view model holds one LoginStep and asks these
    // functions what follows from it.

    enum class LoginStepKind
    {
        Discovering,
        Unreachable,
        Unsupported,
        Local,
        Browser,
        Custom,
        Waiting,
        SignedIn,
        Declined,
        Expired,
        Preparing,
    };

    enum class BrowserMethodKind
    {
        Oidc,
        Addon,
    };

    // A browser sign-in: the server's OIDC provider, or the sign-in add-on
    // for the server's mode, which names its own button.
    struct BrowserMethod final
    {
        BrowserMethodKind Kind{ BrowserMethodKind::Oidc };
        std::wstring AddonLabel;

        friend bool operator==(BrowserMethod const&, BrowserMethod const&) = default;
    };

    // A step the custom-server form can be opened from and goes back to:
    // Discovering, Unreachable, Unsupported, Local or Browser.
    struct RestingStep final
    {
        LoginStepKind Kind{ LoginStepKind::Discovering };
        // Unreachable: the error, shown under "Technical details".
        std::wstring Detail;
        // Unsupported: the server's mode.
        std::wstring Mode;
        // Local and Browser: the error line, empty for none.
        std::wstring Error;
        // Browser.
        std::optional<BrowserMethod> Method;

        friend bool operator==(RestingStep const&, RestingStep const&) = default;
    };

    struct LoginStep final
    {
        LoginStepKind Kind{ LoginStepKind::Discovering };
        std::wstring Detail;
        std::wstring Mode;
        std::wstring Error;
        // Browser, Declined, Expired and a browser Waiting.
        std::optional<BrowserMethod> Method;
        // Custom: where its back link goes.
        std::optional<RestingStep> Back;
        // Waiting: a username and password sign-in rather than the browser.
        bool WaitingLocal{};
        // Waiting in the browser: the sign-in page's address is known, so it
        // can be opened again.
        bool AuthUrlKnown{};
        // SignedIn.
        std::wstring AccountName;
        std::wstring AccountLabel;
        bool Moving{};

        friend bool operator==(LoginStep const&, LoginStep const&) = default;
    };

    [[nodiscard]] LoginStep ToStep(RestingStep const& resting);
    // Null for a step the custom form cannot be opened from.
    [[nodiscard]] std::optional<RestingStep> ToResting(LoginStep const& step);

    // The built-in server's answer: its own accounts, OIDC, an add-on this
    // build carries for the mode (its button label given), or a mode this
    // build lacks.
    [[nodiscard]] RestingStep ReadyStep(std::wstring_view mode, std::optional<std::wstring> addonLabel);
    [[nodiscard]] RestingStep UnreachableStep(std::wstring detail);

    // Discovery's answer replaces the step only while it is still
    // Discovering; a custom form opened meanwhile goes back to the answer
    // instead of a spinner. Anything else is left alone.
    [[nodiscard]] LoginStep ApplyDiscovery(LoginStep const& current, RestingStep const& found);

    // A step to return to: Discovering means the server's answer once it has
    // come.
    [[nodiscard]] RestingStep Settled(RestingStep const& back, std::optional<RestingStep> const& discovered);

    // The custom-server form, from a resting step; null from any other.
    [[nodiscard]] std::optional<LoginStep> OpenCustom(LoginStep const& current);

    // What a failed browser sign-in turns the screen into.
    [[nodiscard]] LoginStep StepAfterBrowserFailure(
        BrowserMethod const& method,
        ::HaloDesktop::Services::Auth::SignInFailure const& failure);

    // The browser method's button: an add-on names itself.
    [[nodiscard]] std::wstring MethodLabel(BrowserMethod const& method);
    // The form's way back: "Back", or "Sign in with X instead" for a browser
    // method whose button reads "Continue with X".
    [[nodiscard]] std::wstring BackLabel(RestingStep const& back);

    // The tagline and feature chips are left out of the two forms.
    [[nodiscard]] bool IntroVisible(LoginStepKind kind) noexcept;
    // Steps that leave Halo usable without an account; the others are mid
    // sign-in or a form whose way out is its own back link.
    [[nodiscard]] bool NoAccountStep(LoginStepKind kind) noexcept;
    // The username and password links: "Use username & password" on the
    // discovering, browser, unsupported and unreachable steps.
    [[nodiscard]] bool PasswordLinkVisible(LoginStepKind kind) noexcept;

    [[nodiscard]] std::wstring WaitingTitle(bool local);
    [[nodiscard]] std::wstring WaitingBody(bool local);
    [[nodiscard]] std::wstring WaitingStatus(bool local);
    [[nodiscard]] std::wstring UnsupportedBody(std::wstring_view mode);

    // The account card: the username with its first letter raised, and the
    // mono caption (the tier when the server has tiers, else the kind of
    // account).
    [[nodiscard]] std::wstring AccountDisplayName(std::wstring_view username);
    [[nodiscard]] std::wstring AccountLabel(bool isAdmin, std::optional<std::wstring_view> plan);
    [[nodiscard]] std::wstring AvatarInitial(std::wstring_view name);

    // Halo without an account: its button ("Continue without an account", or
    // "Back to Halo" when the screen was opened from it) shows on the steps
    // NoAccountStep names; the view model routes the button to one method.
    inline constexpr bool NoAccountModeAvailable = true;

    [[nodiscard]] std::wstring NoAccountLabel(bool openedFromDevice);
    // The signed-in step's status line.
    [[nodiscard]] std::wstring SignedInStatus(bool moving);

    inline constexpr std::wstring_view MissingCredentialsError = L"Enter your username and password.";
    inline constexpr std::wstring_view CustomServerUnreachableError =
        L"Couldn’t reach that server. Check the address and try again.";
    inline constexpr std::wstring_view CustomServerBrowserModeError =
        L"That server signs in through a web page, not a username and password.";
    // The server's mode, when the form may go on to send the password.
    [[nodiscard]] bool CustomServerAcceptsPasswords(std::wstring_view mode) noexcept;
}
