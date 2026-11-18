#include "pch.h"
#include "Services/Auth/SignInAddon.h"

namespace HaloDesktop::Services::Auth
{
    // The public build's definition: no sign-in add-ons. A local build that
    // has src/HaloDesktop/Local/SignInAddons.cpp compiles that file instead of
    // this one (see Services/Auth/SignInAddon.h).
    std::vector<std::shared_ptr<ISignInAddon>> CreateSignInAddons(
        [[maybe_unused]] SignInAddonContext const& context)
    {
        return {};
    }
}
