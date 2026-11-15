#include "Services/AccountLabelPolicy.h"

#include <windows.h>

namespace
{
    // Locale-independent upper case, like JavaScript's toUpperCase for the
    // characters a username or a tier name holds. Text Windows cannot map is
    // returned unchanged.
    std::wstring UpperInvariant(std::wstring_view text)
    {
        if (text.empty()) return {};
        auto const length = static_cast<int>(text.size());
        auto const needed = LCMapStringEx(
            LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, text.data(), length, nullptr, 0, nullptr, nullptr, 0);
        if (needed <= 0) return std::wstring{ text };
        std::wstring result(static_cast<std::size_t>(needed), L'\0');
        auto const written = LCMapStringEx(
            LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, text.data(), length, result.data(), needed, nullptr, nullptr, 0);
        if (written <= 0) return std::wstring{ text };
        result.resize(static_cast<std::size_t>(written));
        return result;
    }
}

namespace HaloDesktop::Services
{
    std::wstring AccountDisplayName(std::wstring_view username)
    {
        if (username.empty()) return {};
        // charAt(0) is one UTF-16 unit; a surrogate half is left as it is.
        auto const first = username.front();
        if (first >= 0xD800 && first <= 0xDFFF) return std::wstring{ username };
        return UpperInvariant(username.substr(0, 1)) + std::wstring{ username.substr(1) };
    }

    std::wstring AccountLabel(std::optional<AccountLabelInput> const& account)
    {
        if (account && !account->Plan.empty()) return UpperInvariant(account->Plan);
        return account && account->IsAdmin ? L"ADMIN \x00B7 HALO ACCOUNT" : L"HALO ACCOUNT";
    }
}
