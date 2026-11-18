#include "pch.h"
#include "Services/Auth/SignInAddonRegistry.h"

#include <algorithm>
#include <utility>

namespace HaloDesktop::Services::Auth
{
    SignInAddonRegistry::SignInAddonRegistry(std::vector<std::shared_ptr<ISignInAddon>> addons)
    {
        for (auto& addon : addons)
        {
            if (!addon)
            {
                continue;
            }
            auto const mode = addon->Mode();
            if (mode.empty() || mode == L"oidc" || mode == L"local")
            {
                continue;
            }
            auto const claimed = std::any_of(m_addons.begin(), m_addons.end(), [&mode](auto const& existing)
            {
                return existing->Mode() == mode;
            });
            if (!claimed)
            {
                m_addons.push_back(std::move(addon));
            }
        }
    }

    std::shared_ptr<ISignInAddon> SignInAddonRegistry::Find(std::wstring_view mode) const
    {
        for (auto const& addon : m_addons)
        {
            if (addon->Mode() == mode)
            {
                return addon;
            }
        }
        return nullptr;
    }

    std::vector<std::shared_ptr<ISignInAddon>> const& SignInAddonRegistry::All() const noexcept
    {
        return m_addons;
    }

    bool SignInAddonRegistry::Empty() const noexcept
    {
        return m_addons.empty();
    }

    std::wstring AddonSessionKind(std::wstring_view mode)
    {
        return std::wstring{ L"addon:" } + std::wstring{ mode };
    }
}
