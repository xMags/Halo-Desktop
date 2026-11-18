#include "Services/Auth/SignInFailurePolicy.h"

#include <algorithm>
#include <cwctype>
#include <utility>

namespace
{
    constexpr std::size_t MaximumShownText = 512;

    bool ContainsIgnoringCase(std::wstring_view text, std::wstring_view needle)
    {
        if (needle.empty() || needle.size() > text.size())
        {
            return needle.empty();
        }
        for (std::size_t start = 0; start + needle.size() <= text.size(); ++start)
        {
            auto matches = true;
            for (std::size_t index = 0; index < needle.size(); ++index)
            {
                if (std::towlower(text[start + index]) != std::towlower(needle[index]))
                {
                    matches = false;
                    break;
                }
            }
            if (matches)
            {
                return true;
            }
        }
        return false;
    }
}

namespace HaloDesktop::Services::Auth
{
    SignInError::SignInError(SignInErrorInfo info)
        : std::runtime_error{ "The sign-in did not finish." },
          m_info(std::move(info))
    {
    }

    SignInErrorInfo const& SignInError::Info() const noexcept
    {
        return m_info;
    }

    SignInError BrowserSignInFailure(BrowserFailureReason reason, std::wstring_view message)
    {
        return SignInError{ SignInErrorInfo{
            .Reason = reason,
            .Refused = false,
            .Code = {},
            .Message = SanitizedSignInText(message),
        } };
    }

    SignInError SignInRefused(std::wstring_view message, std::wstring_view code)
    {
        return SignInError{ SignInErrorInfo{
            .Reason = std::nullopt,
            .Refused = true,
            .Code = std::wstring{ code },
            .Message = SanitizedSignInText(message),
        } };
    }

    SignInError SignInFailed(std::wstring_view message)
    {
        return SignInError{ SignInErrorInfo{
            .Reason = std::nullopt,
            .Refused = false,
            .Code = {},
            .Message = SanitizedSignInText(message),
        } };
    }

    SignInErrorInfo LoopbackWaitFailure(std::wstring_view message)
    {
        SignInErrorInfo info{ .Message = SanitizedSignInText(message) };
        if (ContainsIgnoringCase(message, L"cancelled"))
        {
            info.Reason = BrowserFailureReason::Cancelled;
        }
        else if (ContainsIgnoringCase(message, L"timed out"))
        {
            info.Reason = BrowserFailureReason::Expired;
        }
        return info;
    }

    std::optional<SignInErrorInfo> CallbackFailure(
        std::optional<std::wstring_view> error,
        std::optional<std::wstring_view> errorDescription)
    {
        if (!error || *error != L"access_denied")
        {
            return std::nullopt;
        }
        auto const description = errorDescription ? SanitizedSignInText(*errorDescription) : std::wstring{};
        return SignInErrorInfo{
            .Reason = BrowserFailureReason::Declined,
            .Refused = false,
            .Code = {},
            .Message = description.empty() ? std::wstring{ L"Sign-in was declined" } : description,
        };
    }

    SignInFailure ClassifySignInFailure(SignInErrorInfo const& error)
    {
        if (error.Reason)
        {
            switch (*error.Reason)
            {
            case BrowserFailureReason::Cancelled:
                return SignInFailure{ SignInFailureKind::Cancelled, {} };
            case BrowserFailureReason::Declined:
                return SignInFailure{ SignInFailureKind::Declined, {} };
            case BrowserFailureReason::Expired:
                return SignInFailure{ SignInFailureKind::Expired, {} };
            }
        }
        if (error.Code == L"invalid_grant")
        {
            return SignInFailure{ SignInFailureKind::Expired, {} };
        }
        auto message = SanitizedSignInText(error.Message);
        if (error.Refused)
        {
            return SignInFailure{ SignInFailureKind::Refused, std::move(message) };
        }
        return SignInFailure{
            SignInFailureKind::Unreachable,
            message.empty() ? std::wstring{ L"Unknown error" } : std::move(message),
        };
    }

    std::wstring LocalSignInMessage(std::optional<std::uint16_t> httpStatus)
    {
        if (httpStatus && *httpStatus == 401)
        {
            return L"The username or password is incorrect.";
        }
        if (httpStatus && *httpStatus == 429)
        {
            return L"Too many attempts. Try again later.";
        }
        return L"Can't connect to Halo. Try again.";
    }

    std::wstring SanitizedSignInText(std::wstring_view text)
    {
        std::wstring value;
        value.reserve((std::min)(text.size(), MaximumShownText));
        for (auto const character : text)
        {
            value.push_back(std::iswcntrl(character) != 0 ? L' ' : character);
        }
        auto const first = value.find_first_not_of(L" \t");
        if (first == std::wstring::npos)
        {
            return {};
        }
        auto const last = value.find_last_not_of(L" \t");
        value = value.substr(first, last - first + 1);
        if (value.size() > MaximumShownText)
        {
            value.resize(MaximumShownText);
        }
        return value;
    }
}
