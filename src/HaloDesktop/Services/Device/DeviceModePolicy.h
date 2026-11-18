#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace HaloDesktop::Services::Device
{
    // The rules of Halo used without an account that do not need the session
    // itself (the reference's `auth/session.tsx` and `api.ts`). No I/O.

    // What reading this PC's profile at launch found.
    enum class ProfileRead
    {
        None,
        Found,
        // The profile file is there but cannot be read; it is left untouched.
        Unreadable,
    };

    enum class StartupScreen
    {
        // The sign-in screen.
        SignIn,
        // Halo without an account, on this PC's data.
        Device,
        // The restored account.
        SignedIn,
    };

    struct StartupDecision final
    {
        StartupScreen Screen{ StartupScreen::SignIn };
        // Signed in with a profile still on this PC: its data has not moved
        // into the account yet, and the move is tried again.
        bool RememberProfile{};
        // The sign-in screen says the profile could not be read.
        bool ShowDeviceError{};

        friend bool operator==(StartupDecision const&, StartupDecision const&) = default;
    };

    // The reference's initialState: a restored session wins, then a profile,
    // then the sign-in screen. The reference treats an unreadable profile as
    // none; this app also says so on the sign-in screen, and never offers to
    // replace it.
    [[nodiscard]] StartupDecision DecideStartup(bool sessionRestored, ProfileRead profile) noexcept;

    // topUpDeviceAddons: the list to save when a built-in addon is missing
    // from this PC's own list, which is this PC's list followed by the
    // missing built-ins in their order; empty when none is missing.
    [[nodiscard]] std::optional<std::vector<std::wstring>> TopUpAddonList(std::vector<std::wstring> const& installed);

    // The sign-in screen's line when Halo could not start on this PC.
    [[nodiscard]] std::wstring DeviceSetupError(std::wstring_view detail);
}
