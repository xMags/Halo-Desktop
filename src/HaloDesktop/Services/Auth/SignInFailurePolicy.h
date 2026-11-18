#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace HaloDesktop::Services::Auth
{
    // How a failed sign-in reads on the sign-in screen, mirroring the reference
    // client's `auth/signInFailure.ts`: the user backed out (back to the
    // button), the provider declined, the request expired, the provider refused
    // with a reason, or Halo could not be reached. Pure.

    enum class BrowserFailureReason
    {
        Cancelled,
        Declined,
        Expired,
    };

    // What a failed sign-in step knows about itself. A browser sign-in that
    // ended at the user's or the provider's say has a Reason; a provider that
    // answered with a refusal is Refused (and may carry an OAuth error Code,
    // such as `invalid_grant`); anything else is a failure with only its
    // Message, which the screen shows as the technical detail.
    struct SignInErrorInfo final
    {
        std::optional<BrowserFailureReason> Reason;
        bool Refused{};
        std::wstring Code;
        std::wstring Message;
    };

    // The exception every sign-in step (the browser wait, the OIDC token
    // exchange, a sign-in add-on) throws for a failure it can describe.
    // Anything else thrown is classified by its message alone.
    class SignInError final : public std::runtime_error
    {
    public:
        explicit SignInError(SignInErrorInfo info);

        [[nodiscard]] SignInErrorInfo const& Info() const noexcept;

    private:
        SignInErrorInfo m_info;
    };

    // A browser sign-in that ended at the user's or the provider's say.
    [[nodiscard]] SignInError BrowserSignInFailure(BrowserFailureReason reason, std::wstring_view message);

    // A provider that answered and said no (a disabled account, a bad
    // request): its reason is shown. Sign-in add-ons throw this.
    [[nodiscard]] SignInError SignInRefused(std::wstring_view message, std::wstring_view code = {});

    // A failure that is neither, keeping its message as the detail.
    [[nodiscard]] SignInError SignInFailed(std::wstring_view message);

    enum class SignInFailureKind
    {
        Cancelled,
        Declined,
        Expired,
        Refused,
        Unreachable,
    };

    // Text is the provider's reason for Refused and the technical detail for
    // Unreachable; empty otherwise.
    struct SignInFailure final
    {
        SignInFailureKind Kind{ SignInFailureKind::Unreachable };
        std::wstring Text;
    };

    // The loopback listener's failure, by its message: its cancel and its
    // timeout. Anything else is a real failure and keeps its message.
    [[nodiscard]] SignInErrorInfo LoopbackWaitFailure(std::wstring_view message);

    // The callback's `error` parameter: only a refusal (`access_denied`)
    // counts as declined.
    [[nodiscard]] std::optional<SignInErrorInfo> CallbackFailure(
        std::optional<std::wstring_view> error,
        std::optional<std::wstring_view> errorDescription);

    // Any failed browser sign-in (OIDC or a sign-in add-on). A code that
    // reached the token exchange too late (`invalid_grant`) is an expired
    // request, and a provider's own refusal keeps its reason. Everything else
    // reads as "can't connect", with the message kept as the detail.
    [[nodiscard]] SignInFailure ClassifySignInFailure(SignInErrorInfo const& error);

    // The username and password form's error line, from the HTTP status the
    // server answered with (none for a failure to reach it).
    [[nodiscard]] std::wstring LocalSignInMessage(std::optional<std::uint16_t> httpStatus);

    // Text that came from a server, a provider or a redirect, made safe to
    // show: control characters become spaces, the ends are trimmed and it is
    // cut to 512 characters.
    [[nodiscard]] std::wstring SanitizedSignInText(std::wstring_view text);
}
