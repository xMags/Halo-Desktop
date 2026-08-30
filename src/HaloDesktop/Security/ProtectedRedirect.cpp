#include "Security/ProtectedRedirect.h"

#include <cwchar>

namespace HaloDesktop::Security
{
    bool IsRedirectStatus(std::uint32_t status) noexcept
    {
        return status == 301 || status == 302 || status == 303
            || status == 307 || status == 308;
    }

    bool SameHttpOrigin(
        winrt::Windows::Foundation::Uri const& left,
        winrt::Windows::Foundation::Uri const& right) noexcept
    {
        try
        {
            return _wcsicmp(left.SchemeName().c_str(), right.SchemeName().c_str()) == 0
                && _wcsicmp(left.Host().c_str(), right.Host().c_str()) == 0
                && left.Port() == right.Port();
        }
        catch (...)
        {
            return false;
        }
    }

    std::optional<RedirectTarget> NextRedirectTarget(
        std::wstring const& currentUrl,
        std::wstring const& location,
        int hopsAlreadyFollowed) noexcept
    {
        if (hopsAlreadyFollowed >= MaximumProtectedRedirects
            || location.empty()
            || location.size() > MaximumLocationLength
            || location.find(L'\0') != std::wstring::npos)
        {
            return std::nullopt;
        }

        try
        {
            winrt::Windows::Foundation::Uri const current{ winrt::hstring{ currentUrl } };
            auto const next = current.CombineUri(winrt::hstring{ location });
            auto const nextScheme = next.SchemeName();
            if (next.Host().empty()
                || (_wcsicmp(nextScheme.c_str(), L"http") != 0
                    && _wcsicmp(nextScheme.c_str(), L"https") != 0)
                || (_wcsicmp(current.SchemeName().c_str(), L"https") == 0
                    && _wcsicmp(nextScheme.c_str(), L"https") != 0))
            {
                return std::nullopt;
            }
            return RedirectTarget{
                std::wstring{ next.AbsoluteUri() },
                SameHttpOrigin(current, next),
            };
        }
        catch (...)
        {
            return std::nullopt;
        }
    }
}
