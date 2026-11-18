#include "ViewModels/LoginStepsPolicy.h"

#include <cwctype>
#include <utility>

namespace
{
    using HaloDesktop::ViewModels::LoginStepKind;

    bool IsResting(LoginStepKind kind) noexcept
    {
        return kind == LoginStepKind::Discovering || kind == LoginStepKind::Unreachable
            || kind == LoginStepKind::Unsupported || kind == LoginStepKind::Local
            || kind == LoginStepKind::Browser;
    }

    bool IsWordCharacter(wchar_t character) noexcept
    {
        return (character >= L'a' && character <= L'z') || (character >= L'A' && character <= L'Z')
            || (character >= L'0' && character <= L'9') || character == L'_';
    }

    std::wstring Uppercase(std::wstring_view value)
    {
        std::wstring result;
        result.reserve(value.size());
        for (auto const character : value)
        {
            result.push_back(static_cast<wchar_t>(std::towupper(character)));
        }
        return result;
    }
}

namespace HaloDesktop::ViewModels
{
    LoginStep ToStep(RestingStep const& resting)
    {
        LoginStep step;
        step.Kind = resting.Kind;
        step.Detail = resting.Detail;
        step.Mode = resting.Mode;
        step.Error = resting.Error;
        step.Method = resting.Method;
        return step;
    }

    std::optional<RestingStep> ToResting(LoginStep const& step)
    {
        if (!IsResting(step.Kind))
        {
            return std::nullopt;
        }
        return RestingStep{
            .Kind = step.Kind,
            .Detail = step.Detail,
            .Mode = step.Mode,
            .Error = step.Error,
            .Method = step.Method,
        };
    }

    RestingStep ReadyStep(std::wstring_view mode, std::optional<std::wstring> addonLabel)
    {
        if (mode == L"local")
        {
            return RestingStep{ .Kind = LoginStepKind::Local };
        }
        if (mode == L"oidc")
        {
            return RestingStep{ .Kind = LoginStepKind::Browser, .Method = BrowserMethod{ BrowserMethodKind::Oidc, {} } };
        }
        if (addonLabel)
        {
            return RestingStep{
                .Kind = LoginStepKind::Browser,
                .Method = BrowserMethod{ BrowserMethodKind::Addon, std::move(*addonLabel) },
            };
        }
        return RestingStep{ .Kind = LoginStepKind::Unsupported, .Mode = std::wstring{ mode } };
    }

    RestingStep UnreachableStep(std::wstring detail)
    {
        return RestingStep{ .Kind = LoginStepKind::Unreachable, .Detail = std::move(detail) };
    }

    LoginStep ApplyDiscovery(LoginStep const& current, RestingStep const& found)
    {
        if (current.Kind == LoginStepKind::Discovering)
        {
            return ToStep(found);
        }
        if (current.Kind == LoginStepKind::Custom && current.Back
            && current.Back->Kind == LoginStepKind::Discovering)
        {
            auto next = current;
            next.Back = found;
            return next;
        }
        return current;
    }

    RestingStep Settled(RestingStep const& back, std::optional<RestingStep> const& discovered)
    {
        if (back.Kind == LoginStepKind::Discovering && discovered)
        {
            return *discovered;
        }
        return back;
    }

    std::optional<LoginStep> OpenCustom(LoginStep const& current)
    {
        auto resting = ToResting(current);
        if (!resting)
        {
            return std::nullopt;
        }
        LoginStep custom;
        custom.Kind = LoginStepKind::Custom;
        custom.Back = std::move(resting);
        return custom;
    }

    LoginStep StepAfterBrowserFailure(
        BrowserMethod const& method,
        ::HaloDesktop::Services::Auth::SignInFailure const& failure)
    {
        using ::HaloDesktop::Services::Auth::SignInFailureKind;
        LoginStep step;
        switch (failure.Kind)
        {
        case SignInFailureKind::Cancelled:
            step.Kind = LoginStepKind::Browser;
            step.Method = method;
            return step;
        case SignInFailureKind::Refused:
            step.Kind = LoginStepKind::Browser;
            step.Method = method;
            step.Error = failure.Text;
            return step;
        case SignInFailureKind::Declined:
            step.Kind = LoginStepKind::Declined;
            step.Method = method;
            return step;
        case SignInFailureKind::Expired:
            step.Kind = LoginStepKind::Expired;
            step.Method = method;
            return step;
        case SignInFailureKind::Unreachable:
            break;
        }
        step.Kind = LoginStepKind::Unreachable;
        step.Detail = failure.Text;
        return step;
    }

    std::wstring MethodLabel(BrowserMethod const& method)
    {
        return method.Kind == BrowserMethodKind::Addon ? method.AddonLabel : std::wstring{ L"Continue in browser" };
    }

    std::wstring BackLabel(RestingStep const& back)
    {
        if (back.Kind != LoginStepKind::Browser || !back.Method)
        {
            return L"Back";
        }
        auto label = MethodLabel(*back.Method);
        // `/^Continue\b/`: the word itself, not a longer one starting with it.
        constexpr std::wstring_view prefix = L"Continue";
        if (label.starts_with(prefix) && (label.size() == prefix.size() || !IsWordCharacter(label[prefix.size()])))
        {
            label.replace(0, prefix.size(), L"Sign in");
        }
        return label + L" instead";
    }

    bool IntroVisible(LoginStepKind kind) noexcept
    {
        return kind != LoginStepKind::Custom && kind != LoginStepKind::Local;
    }

    bool NoAccountStep(LoginStepKind kind) noexcept
    {
        return IsResting(kind) || kind == LoginStepKind::Declined || kind == LoginStepKind::Expired;
    }

    bool PasswordLinkVisible(LoginStepKind kind) noexcept
    {
        return kind == LoginStepKind::Discovering || kind == LoginStepKind::Browser
            || kind == LoginStepKind::Unsupported || kind == LoginStepKind::Unreachable;
    }

    std::wstring WaitingTitle(bool local)
    {
        return local ? L"Signing in" : L"Check your browser";
    }

    std::wstring WaitingBody(bool local)
    {
        return local
            ? L"Halo is checking your account with your server."
            : L"We opened a sign-in page in your browser. Approve it there and Halo carries on by itself.";
    }

    std::wstring WaitingStatus(bool local)
    {
        return local ? L"CHECKING ACCOUNT" : L"WAITING FOR BROWSER";
    }

    std::wstring UnsupportedBody(std::wstring_view mode)
    {
        return std::wstring{ L"This server signs in with “" } + std::wstring{ mode }
            + L"”, which this build of Halo doesn’t include.";
    }

    std::wstring AccountDisplayName(std::wstring_view username)
    {
        std::wstring name{ username };
        if (!name.empty())
        {
            name.front() = static_cast<wchar_t>(std::towupper(name.front()));
        }
        return name;
    }

    std::wstring AccountLabel(bool isAdmin, std::optional<std::wstring_view> plan)
    {
        if (plan && !plan->empty())
        {
            return Uppercase(*plan);
        }
        return isAdmin ? L"ADMIN · HALO ACCOUNT" : L"HALO ACCOUNT";
    }

    std::wstring AvatarInitial(std::wstring_view name)
    {
        auto const first = name.empty() ? L'H' : name.front();
        return std::wstring(1, static_cast<wchar_t>(std::towupper(first)));
    }

    std::wstring NoAccountLabel(bool openedFromDevice)
    {
        return openedFromDevice ? L"Back to Halo" : L"Continue without an account";
    }

    std::wstring SignedInStatus(bool moving)
    {
        return moving
            ? L"MOVING THIS PC’S LIBRARY INTO YOUR ACCOUNT…"
            : L"OPENING YOUR LIBRARY…";
    }

    bool CustomServerAcceptsPasswords(std::wstring_view mode) noexcept
    {
        return mode == L"local";
    }
}
