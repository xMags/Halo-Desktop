#pragma once

#include "Services/Auth/SignInAddon.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace HaloDesktop::Services::Auth
{
    // The build's sign-in add-ons, looked up by the /auth/config mode they sign
    // in for. Empty in a public build. Immutable after construction, so it is
    // safe to read from any thread.
    class SignInAddonRegistry final
    {
    public:
        // Drops null entries, add-ons with no mode or a built-in mode ("oidc",
        // "local"), and any later add-on for a mode an earlier one claimed.
        explicit SignInAddonRegistry(std::vector<std::shared_ptr<ISignInAddon>> addons);

        [[nodiscard]] std::shared_ptr<ISignInAddon> Find(std::wstring_view mode) const;
        [[nodiscard]] std::vector<std::shared_ptr<ISignInAddon>> const& All() const noexcept;
        [[nodiscard]] bool Empty() const noexcept;

    private:
        std::vector<std::shared_ptr<ISignInAddon>> m_addons;
    };

    // The session kind an add-on's sessions run under: `addon:<mode>`.
    [[nodiscard]] std::wstring AddonSessionKind(std::wstring_view mode);
}
