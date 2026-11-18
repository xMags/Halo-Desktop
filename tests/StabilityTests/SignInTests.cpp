#include "SignInTests.h"

#include "Services/Auth/LoopbackLandingPage.h"
#include "Services/Auth/ServerAddressPolicy.h"
#include "Services/Auth/SignInAddon.h"
#include "Services/Auth/SignInAddonRegistry.h"
#include "Services/Auth/SignInFailurePolicy.h"
#include "ViewModels/LoginStepsPolicy.h"

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    void Require(bool condition, char const* message)
    {
        if (!condition)
        {
            throw std::runtime_error{ message };
        }
    }

    namespace Auth = HaloDesktop::Services::Auth;
    namespace Steps = HaloDesktop::ViewModels;

    bool Contains(std::wstring_view text, std::wstring_view part)
    {
        return text.find(part) != std::wstring_view::npos;
    }

    std::optional<std::wstring> Address(std::wstring_view raw)
    {
        return Auth::ParseServerAddress(raw).Address;
    }

    std::wstring AddressError(std::wstring_view raw)
    {
        auto const result = Auth::ParseServerAddress(raw);
        return result.Address ? std::wstring{} : result.Error;
    }

    // signIn.test.ts: "a typed server address gets https, keeps its path and
    // refuses what is not an address".
    void TestTypedServerAddress()
    {
        Require(Address(L" halo.example.com/ ") == L"https://halo.example.com", "a bare host did not get https");
        Require(Address(L"https://halo.example.com/api/") == L"https://halo.example.com/api", "a path was not kept");
        Require(Address(L"http://localhost:8787") == L"http://localhost:8787", "plain http on this PC was refused");
        Require(Contains(AddressError(L""), L"Enter your server"), "an empty address was not asked for");
        Require(Contains(AddressError(L"http://halo.example.com"), L"https"), "plain http to another machine was accepted");
        Require(Contains(AddressError(L"https://user:pw@halo.example.com"), L"only the server"), "credentials were accepted");
        Require(Contains(AddressError(L"https://halo.example.com/?x=1"), L"only the server"), "a query was accepted");

        // The four sentences, word for word.
        Require(AddressError(L"   ") == L"Enter your server’s address.", "the empty sentence changed");
        Require(AddressError(L"https://exa mple.com") == L"That isn’t a server address.", "the invalid sentence changed");
        Require(AddressError(L"ftp://halo.example.com")
                == L"Use an https address. Plain http only works for a server on this PC.",
            "the insecure sentence changed");
        Require(AddressError(L"https://halo.example.com#top") == L"Enter only the server’s address.",
            "the extra sentence changed");

        // As a URL parser reads them.
        Require(Address(L"HTTPS://Halo.Example.COM:443/") == L"https://halo.example.com", "the default port or case was kept");
        Require(Address(L"http://127.0.0.1:18790") == L"http://127.0.0.1:18790", "loopback http was refused");
        Require(Address(L"http://[::1]:8787/") == L"http://[::1]:8787", "IPv6 loopback was refused");
        Require(Address(L"localhost:8787") == L"https://localhost:8787", "a host and port did not get https");
        Require(Address(L"https://halo.example.com/a/../b/./") == L"https://halo.example.com/b", "dot segments were not resolved");
        Require(Address(L"https://@halo.example.com") == L"https://halo.example.com", "an empty user info was refused");
        Require(Address(L"https://halo.example.com:8443") == L"https://halo.example.com:8443", "a port was dropped");
        Require(!Address(L"https://halo.example.com:99999"), "an impossible port was accepted");
        Require(!Address(L"https://"), "an address without a host was accepted");
        Require(!Address(L"https://halo.example.com\\api"), "a backslash was accepted");
        Require(!Address(std::wstring(Auth::MaximumServerAddressLength + 1, L'a')), "an over-long address was accepted");
        Require(Contains(AddressError(L"http://192.168.1.4"), L"https"), "plain http on the local network was accepted");

        Require(Auth::IsLoopbackHost(L"localhost") && Auth::IsLoopbackHost(L"127.0.0.1") && Auth::IsLoopbackHost(L"[::1]"),
            "a loopback host was not recognised");
        Require(!Auth::IsLoopbackHost(L"localhost.example.com"), "a lookalike host was loopback");
    }

    // api.ts forgetSessionsFromAnotherServer, with the record of which
    // built-in server the saved sessions were for.
    void TestSessionsFromAnotherServer()
    {
        Require(Auth::CheckSessionServer(std::nullopt, L"https://halo.example.com") == Auth::SessionServerCheck::Unrecorded,
            "a missing record was not unrecorded");
        Require(Auth::CheckSessionServer(std::wstring_view{}, L"https://halo.example.com") == Auth::SessionServerCheck::Unrecorded,
            "an empty record was not unrecorded");
        Require(Auth::CheckSessionServer(std::wstring_view{ L"https://halo.example.com/" }, L"https://halo.example.com")
                == Auth::SessionServerCheck::Matches,
            "a trailing slash made the same server another");
        Require(Auth::CheckSessionServer(std::wstring_view{ L"https://other.example.com" }, L"https://halo.example.com")
                == Auth::SessionServerCheck::Different,
            "another server's sessions were kept");
    }

    // signIn.test.ts: failed sign-ins, as the sign-in screen tells them apart.
    void TestFailureClassification()
    {
        using Auth::SignInFailureKind;

        auto const cancelled = Auth::LoopbackWaitFailure(L"sign-in cancelled");
        Require(cancelled.Reason == Auth::BrowserFailureReason::Cancelled
                && Auth::ClassifySignInFailure(cancelled).Kind == SignInFailureKind::Cancelled,
            "the listener's cancel was not cancelled");
        Require(Auth::ClassifySignInFailure(Auth::LoopbackWaitFailure(L"sign-in timed out waiting for the browser redirect")).Kind
                == SignInFailureKind::Expired,
            "the listener's timeout was not expired");
        // This app's own listener words them so.
        Require(Auth::ClassifySignInFailure(Auth::LoopbackWaitFailure(L"Halo sign-in was cancelled.")).Kind == SignInFailureKind::Cancelled
                && Auth::ClassifySignInFailure(Auth::LoopbackWaitFailure(L"Halo sign-in timed out.")).Kind == SignInFailureKind::Expired,
            "the native listener's cancel or timeout was misread");

        auto const bind = Auth::LoopbackWaitFailure(L"cannot bind 127.0.0.1:17871: in use (is another sign-in pending?)");
        Require(!bind.Reason, "a listener that could not start was a browser failure");
        auto const bindFailure = Auth::ClassifySignInFailure(bind);
        Require(bindFailure.Kind == SignInFailureKind::Unreachable
                && bindFailure.Text == L"cannot bind 127.0.0.1:17871: in use (is another sign-in pending?)",
            "a listener that could not start lost its message");

        auto const declined = Auth::CallbackFailure(std::wstring_view{ L"access_denied" }, std::nullopt);
        Require(declined && Auth::ClassifySignInFailure(*declined).Kind == SignInFailureKind::Declined,
            "access_denied was not declined");
        Require(!Auth::CallbackFailure(std::wstring_view{ L"server_error" }, std::nullopt), "another callback error was declined");
        Require(!Auth::CallbackFailure(std::nullopt, std::nullopt), "a callback without an error was declined");

        // A code redeemed too late reads as an expired request, from any provider.
        Require(Auth::ClassifySignInFailure(Auth::SignInRefused(L"Code is expired", L"invalid_grant").Info()).Kind
                == SignInFailureKind::Expired,
            "invalid_grant from the token endpoint was not expired");
        Require(Auth::ClassifySignInFailure(Auth::SignInRefused(L"That sign-in expired.", L"invalid_grant").Info()).Kind
                == SignInFailureKind::Expired,
            "an add-on's invalid_grant was not expired");

        // A provider's refusal keeps its reason.
        auto const refused = Auth::ClassifySignInFailure(Auth::SignInRefused(L"This account is disabled.").Info());
        Require(refused.Kind == SignInFailureKind::Refused && refused.Text == L"This account is disabled.",
            "a refusal lost its reason");
        auto const client = Auth::ClassifySignInFailure(Auth::SignInRefused(L"invalid_client", L"invalid_client").Info());
        Require(client.Kind == SignInFailureKind::Refused && client.Text == L"invalid_client",
            "the token endpoint's refusal lost its reason");

        // Anything else is can't connect, with the error as the detail.
        auto const other = Auth::ClassifySignInFailure(Auth::SignInFailed(L"error sending request").Info());
        Require(other.Kind == SignInFailureKind::Unreachable && other.Text == L"error sending request",
            "a failure lost its detail");
        Require(Auth::ClassifySignInFailure(Auth::SignInErrorInfo{}).Text == L"Unknown error", "an empty failure had no detail");

        Require(Auth::BrowserSignInFailure(Auth::BrowserFailureReason::Declined, L"no").Info().Reason
                == Auth::BrowserFailureReason::Declined,
            "a browser failure lost its reason");
    }

    void TestLocalFormErrorsAndShownText()
    {
        Require(Auth::LocalSignInMessage(std::uint16_t{ 401 }) == L"The username or password is incorrect.", "the 401 sentence changed");
        Require(Auth::LocalSignInMessage(std::uint16_t{ 429 }) == L"Too many attempts. Try again later.", "the 429 sentence changed");
        Require(Auth::LocalSignInMessage(std::nullopt) == L"Can't connect to Halo. Try again.", "the offline sentence changed");
        Require(Auth::LocalSignInMessage(std::uint16_t{ 500 }) == L"Can't connect to Halo. Try again.", "a server error read differently");

        Require(Auth::SanitizedSignInText(L"  line\r\nbreak\t ") == L"line  break", "control characters reached the screen");
        Require(Auth::SanitizedSignInText(std::wstring(600, L'x')).size() == 512, "an over-long text was not cut");
    }

    // Login.tsx: the steps.
    void TestLoginSteps()
    {
        using Steps::LoginStepKind;

        Require(Steps::ReadyStep(L"local", std::nullopt).Kind == LoginStepKind::Local, "local mode was not the form");
        auto const oidc = Steps::ReadyStep(L"oidc", std::nullopt);
        Require(oidc.Kind == LoginStepKind::Browser && oidc.Method && oidc.Method->Kind == Steps::BrowserMethodKind::Oidc,
            "oidc mode was not the browser");
        auto const addon = Steps::ReadyStep(L"example", std::wstring{ L"Continue with Example" });
        Require(addon.Kind == LoginStepKind::Browser && addon.Method && addon.Method->Kind == Steps::BrowserMethodKind::Addon
                && Steps::MethodLabel(*addon.Method) == L"Continue with Example",
            "an installed add-on's mode was not its button");
        auto const unsupported = Steps::ReadyStep(L"example", std::nullopt);
        Require(unsupported.Kind == LoginStepKind::Unsupported && unsupported.Mode == L"example",
            "a mode this build lacks was not unsupported");
        Require(Steps::MethodLabel(*oidc.Method) == L"Continue in browser", "the browser button's label changed");

        // 966abaa: discovery fills in the step only while it is still discovering.
        Steps::LoginStep discovering;
        Require(Steps::ApplyDiscovery(discovering, oidc) == Steps::ToStep(oidc), "discovery did not settle the spinner");
        auto custom = Steps::OpenCustom(discovering);
        Require(custom && custom->Kind == LoginStepKind::Custom && custom->Back
                && custom->Back->Kind == LoginStepKind::Discovering,
            "the form did not open from the spinner");
        auto const answered = Steps::ApplyDiscovery(*custom, oidc);
        Require(answered.Kind == LoginStepKind::Custom && answered.Back == oidc,
            "discovery replaced an open form, or its back link still went to a spinner");
        auto const openedFromLocal = Steps::OpenCustom(Steps::ToStep(Steps::ReadyStep(L"local", std::nullopt)));
        Require(openedFromLocal && Steps::ApplyDiscovery(*openedFromLocal, oidc) == *openedFromLocal,
            "discovery changed a form opened from a settled step");
        Steps::LoginStep waiting;
        waiting.Kind = LoginStepKind::Waiting;
        Require(Steps::ApplyDiscovery(waiting, oidc) == waiting, "discovery replaced a sign-in under way");
        Require(!Steps::OpenCustom(waiting), "the form opened from a sign-in under way");

        Steps::RestingStep const spinner{};
        Require(Steps::Settled(spinner, oidc) == oidc, "the back link went to an endless spinner");
        Require(Steps::Settled(spinner, std::nullopt) == spinner, "the back link invented an answer");
        Require(Steps::Settled(unsupported, oidc) == unsupported, "a settled back link was replaced");

        Require(Steps::BackLabel(spinner) == L"Back", "the plain back link changed");
        Require(Steps::BackLabel(oidc) == L"Sign in in browser instead", "the browser back link changed");
        Require(Steps::BackLabel(addon) == L"Sign in with Example instead", "an add-on's back link changed");
        auto const continued = Steps::ReadyStep(L"example", std::wstring{ L"Continued access" });
        Require(Steps::BackLabel(continued) == L"Continued access instead", "a longer word was rewritten");

        using Auth::SignInFailure;
        using Auth::SignInFailureKind;
        auto const method = *oidc.Method;
        auto const back = Steps::StepAfterBrowserFailure(method, SignInFailure{ SignInFailureKind::Cancelled, {} });
        Require(back.Kind == LoginStepKind::Browser && back.Error.empty() && back.Method == method, "a cancel did not go back to the button");
        auto const reason = Steps::StepAfterBrowserFailure(method, SignInFailure{ SignInFailureKind::Refused, L"No." });
        Require(reason.Kind == LoginStepKind::Browser && reason.Error == L"No.", "a refusal did not show its reason");
        Require(Steps::StepAfterBrowserFailure(method, SignInFailure{ SignInFailureKind::Declined, {} }).Kind == LoginStepKind::Declined
                && Steps::StepAfterBrowserFailure(method, SignInFailure{ SignInFailureKind::Expired, {} }).Kind == LoginStepKind::Expired,
            "declined or expired lost their steps");
        auto const unreachable = Steps::StepAfterBrowserFailure(method, SignInFailure{ SignInFailureKind::Unreachable, L"offline" });
        Require(unreachable.Kind == LoginStepKind::Unreachable && unreachable.Detail == L"offline", "can't connect lost its detail");

        Require(!Steps::IntroVisible(LoginStepKind::Local) && !Steps::IntroVisible(LoginStepKind::Custom)
                && Steps::IntroVisible(LoginStepKind::Browser) && Steps::IntroVisible(LoginStepKind::Waiting),
            "the intro was shown on a form or hidden elsewhere");
        for (auto const kind : { LoginStepKind::Discovering, LoginStepKind::Unreachable, LoginStepKind::Unsupported,
                 LoginStepKind::Local, LoginStepKind::Browser, LoginStepKind::Declined, LoginStepKind::Expired })
        {
            Require(Steps::NoAccountStep(kind), "a resting step lost the way on without an account");
        }
        for (auto const kind : { LoginStepKind::Custom, LoginStepKind::Waiting, LoginStepKind::SignedIn, LoginStepKind::Preparing })
        {
            Require(!Steps::NoAccountStep(kind), "a step under way offered the way on without an account");
        }
        Require(Steps::PasswordLinkVisible(LoginStepKind::Discovering) && Steps::PasswordLinkVisible(LoginStepKind::Browser)
                && Steps::PasswordLinkVisible(LoginStepKind::Unsupported) && Steps::PasswordLinkVisible(LoginStepKind::Unreachable)
                && !Steps::PasswordLinkVisible(LoginStepKind::Local) && !Steps::PasswordLinkVisible(LoginStepKind::Declined),
            "the username and password link was on the wrong steps");
        // Halo without an account is not in this build yet.
        Require(!Steps::NoAccountModeAvailable, "the way on without an account was turned on");
        Require(Steps::NoAccountLabel(false) == L"Continue without an account" && Steps::NoAccountLabel(true) == L"Back to Halo",
            "the no-account button's copy changed");
    }

    void TestLoginCopy()
    {
        Require(Steps::WaitingTitle(true) == L"Signing in" && Steps::WaitingTitle(false) == L"Check your browser", "the waiting heads changed");
        Require(Steps::WaitingBody(true) == L"Halo is checking your account with your server.", "the local waiting body changed");
        Require(Steps::WaitingBody(false)
                == L"We opened a sign-in page in your browser. Approve it there and Halo carries on by itself.",
            "the browser waiting body changed");
        Require(Steps::WaitingStatus(true) == L"CHECKING ACCOUNT" && Steps::WaitingStatus(false) == L"WAITING FOR BROWSER",
            "the waiting status changed");
        Require(Steps::UnsupportedBody(L"example")
                == L"This server signs in with “example”, which this build of Halo doesn’t include.",
            "the unsupported body changed");
        Require(Steps::SignedInStatus(false) == L"OPENING YOUR LIBRARY…"
                && Steps::SignedInStatus(true) == L"MOVING THIS PC’S LIBRARY INTO YOUR ACCOUNT…",
            "the signed-in status changed");
        Require(Steps::AccountDisplayName(L"kenneth") == L"Kenneth" && Steps::AccountDisplayName(L"").empty(),
            "the account name was not raised");
        Require(Steps::AccountLabel(false, std::wstring_view{ L"premium" }) == L"PREMIUM", "a tier did not name the account");
        Require(Steps::AccountLabel(true, std::nullopt) == L"ADMIN · HALO ACCOUNT"
                && Steps::AccountLabel(false, std::nullopt) == L"HALO ACCOUNT",
            "the account kind changed");
        Require(Steps::AvatarInitial(L"kenneth") == L"K" && Steps::AvatarInitial(L"") == L"H", "the avatar initial changed");
        Require(Steps::CustomServerAcceptsPasswords(L"local") && !Steps::CustomServerAcceptsPasswords(L"oidc"),
            "the custom form's mode check changed");
        Require(Steps::MissingCredentialsError == L"Enter your username and password.", "the missing credentials sentence changed");
        Require(Steps::CustomServerUnreachableError == L"Couldn’t reach that server. Check the address and try again.",
            "the unreachable server sentence changed");
        Require(Steps::CustomServerBrowserModeError == L"That server signs in through a web page, not a username and password.",
            "the web page sentence changed");
    }

    // oauth.rs's tests for the page the browser lands on.
    void TestLandingPage()
    {
        auto const signedIn = Auth::LoopbackLandingPage("/callback?code=abc&state=xyz");
        Require(signedIn.find("You&rsquo;re signed in") != std::string::npos, "a code did not get the signed-in page");
        Require(signedIn.find("{{") == std::string::npos, "a placeholder was left on the page");

        auto const failed = Auth::LoopbackLandingPage("/callback?error=access_denied&error_description=%3Cscript%3E&state=xyz");
        Require(failed.find("didn&rsquo;t finish") != std::string::npos, "a provider error did not get the unfinished page");
        Require(failed.find("access_denied") == std::string::npos && failed.find("script%3E") == std::string::npos
                && failed.find("xyz") == std::string::npos,
            "something from the request reached the page");
        Require(Auth::LoopbackLandingPage("/callback?code=error_x&state=s").find("You&rsquo;re signed in") != std::string::npos,
            "a value that merely starts with error counted as one");
        Require(Auth::CallbackReportsError("/callback?state=s&error") && !Auth::CallbackReportsError("/callback"),
            "the error key was misread");

        auto const response = Auth::LoopbackLandingResponse("/callback?code=abc");
        auto const page = Auth::LoopbackLandingPage("/callback?code=abc");
        Require(response.find("cache-control: no-store\r\n") != std::string::npos, "the landing page could be cached");
        Require(response.find("content-length: " + std::to_string(page.size()) + "\r\n") != std::string::npos
                && response.ends_with(page),
            "the landing response did not carry the page");
    }

    class FakeAddon final : public Auth::ISignInAddon
    {
    public:
        FakeAddon(std::wstring mode, std::wstring label)
            : m_mode(std::move(mode)), m_label(std::move(label))
        {
        }

        [[nodiscard]] std::wstring Mode() const override { return m_mode; }
        [[nodiscard]] std::wstring ButtonLabel() const override { return m_label; }
        [[nodiscard]] concurrency::task<void> SignInAsync(
            winrt::hstring, Auth::AddonAuthConfig, Auth::SignInOpenedHandler, std::stop_token) override
        {
            return concurrency::task_from_result();
        }
        [[nodiscard]] concurrency::task<bool> RestoreAsync() override { return concurrency::task_from_result(false); }
        [[nodiscard]] concurrency::task<void> ClearAsync() override { return concurrency::task_from_result(); }
        [[nodiscard]] concurrency::task<void> SignOutAsync() override { return concurrency::task_from_result(); }
        [[nodiscard]] concurrency::task<std::optional<winrt::hstring>> AccessTokenAsync() override
        {
            return concurrency::task_from_result(std::optional<winrt::hstring>{});
        }
        [[nodiscard]] concurrency::task<std::optional<winrt::hstring>> RefreshAccessTokenAsync() override
        {
            return concurrency::task_from_result(std::optional<winrt::hstring>{});
        }
        [[nodiscard]] std::optional<std::wstring> DownloadOwner() const override { return std::nullopt; }
        void SetDownloadOwner(std::wstring) override {}

    private:
        std::wstring m_mode;
        std::wstring m_label;
    };

    void TestSignInAddonRegistry()
    {
        // A public build has none, so any other mode is unsupported.
        auto const publicAddons = Auth::CreateSignInAddons(Auth::SignInAddonContext{});
        Require(publicAddons.empty(), "a public build carried a sign-in add-on");
        Auth::SignInAddonRegistry const empty{ publicAddons };
        Require(empty.Empty() && !empty.Find(L"example"), "the empty registry found an add-on");

        std::vector<std::shared_ptr<Auth::ISignInAddon>> addons{
            nullptr,
            std::make_shared<FakeAddon>(L"", L"Nameless"),
            std::make_shared<FakeAddon>(L"oidc", L"Not mine"),
            std::make_shared<FakeAddon>(L"example", L"Continue with Example"),
            std::make_shared<FakeAddon>(L"example", L"A second claim"),
            std::make_shared<FakeAddon>(L"other", L"Continue with Other"),
        };
        Auth::SignInAddonRegistry const registry{ std::move(addons) };
        Require(registry.All().size() == 2, "the registry kept an add-on it should have dropped");
        auto const found = registry.Find(L"example");
        Require(found && found->ButtonLabel() == L"Continue with Example", "the first add-on for a mode did not win");
        Require(!registry.Find(L"oidc") && !registry.Find(L"local"), "an add-on took a built-in mode");
        Require(Auth::AddonSessionKind(L"example") == L"addon:example", "the add-on session kind changed");
    }
}

void RunSignInTests()
{
    TestTypedServerAddress();
    TestSessionsFromAnotherServer();
    TestFailureClassification();
    TestLocalFormErrorsAndShownText();
    TestLoginSteps();
    TestLoginCopy();
    TestLandingPage();
    TestSignInAddonRegistry();
}
