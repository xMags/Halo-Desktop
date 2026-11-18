#include "pch.h"
#include "Services/Device/DeviceModePolicy.h"

#include "Services/AddonListPolicy.h"

#include <algorithm>

namespace HaloDesktop::Services::Device
{
    StartupDecision DecideStartup(bool sessionRestored, ProfileRead profile) noexcept
    {
        if (sessionRestored)
        {
            return { StartupScreen::SignedIn, profile == ProfileRead::Found, false };
        }
        if (profile == ProfileRead::Found)
        {
            return { StartupScreen::Device, false, false };
        }
        return { StartupScreen::SignIn, false, profile == ProfileRead::Unreadable };
    }

    std::optional<std::vector<std::wstring>> TopUpAddonList(std::vector<std::wstring> const& installed)
    {
        auto result = installed;
        for (auto const url : ::HaloDesktop::Services::DefaultAddonUrls)
        {
            if (std::find(installed.begin(), installed.end(), url) == installed.end())
            {
                result.emplace_back(url);
            }
        }
        if (result.size() == installed.size())
        {
            return std::nullopt;
        }
        return result;
    }

    std::wstring DeviceSetupError(std::wstring_view detail)
    {
        return L"Halo could not set up on this PC: " + std::wstring{ detail };
    }
}
