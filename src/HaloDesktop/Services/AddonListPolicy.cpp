#include "Services/AddonListPolicy.h"

#include <algorithm>

namespace HaloDesktop::Services
{
    bool IsDefaultAddonUrl(std::wstring_view transportUrl) noexcept
    {
        return std::find(DefaultAddonUrls.begin(), DefaultAddonUrls.end(), transportUrl) != DefaultAddonUrls.end();
    }

    AddonListEditability EvaluateAddonEditability(
        std::span<AddonEditInput const> addons,
        bool isAdmin) noexcept
    {
        auto const complete = [addons](bool global)
        {
            return std::all_of(addons.begin(), addons.end(), [global](AddonEditInput const& addon)
            {
                return addon.IsGlobal != global || addon.HasTransportUrl;
            });
        };
        return AddonListEditability{
            .User = complete(false),
            .Global = isAdmin && complete(true),
        };
    }

    bool CanEditAddonInScope(AddonListEditability editability, bool isGlobal) noexcept
    {
        return isGlobal ? editability.Global : editability.User;
    }
}
