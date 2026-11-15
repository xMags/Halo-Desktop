#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace HaloDesktop::Services
{
    // How the account row names Halo used without an account.
    inline constexpr std::wstring_view DeviceAccountName = L"This PC";
    inline constexpr std::wstring_view DeviceAccountLabel = L"NOT SIGNED IN";

    struct AccountLabelInput final
    {
        bool IsAdmin{};
        // The account's tier from a server that has tiers; empty otherwise.
        std::wstring_view Plan;
    };

    // The username with its first letter raised, as the account card shows it.
    [[nodiscard]] std::wstring AccountDisplayName(std::wstring_view username);

    // The mono caption under the account name. A server that has tiers reports
    // the account's, which then says it all (`PREMIUM`); without one the
    // caption names the account kind and whether it administers this server.
    [[nodiscard]] std::wstring AccountLabel(std::optional<AccountLabelInput> const& account);
}
