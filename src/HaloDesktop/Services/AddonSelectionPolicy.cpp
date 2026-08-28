#include "Services/AddonSelectionPolicy.h"

#include <unordered_map>

namespace HaloDesktop::Services
{
    std::vector<std::size_t> SelectDistinctAddons(std::span<AddonIdentity const> addons)
    {
        std::vector<std::size_t> selected;
        selected.reserve(addons.size());
        // Maps a manifest id onto the slot it already holds in the result, so a
        // duplicate can replace the winner without disturbing the ordering.
        std::unordered_map<std::wstring, std::size_t> slots;
        for (std::size_t index = 0; index < addons.size(); ++index)
        {
            auto const& addon = addons[index];
            if (addon.ManifestId.empty())
            {
                selected.push_back(index);
                continue;
            }

            auto const [slot, inserted] = slots.try_emplace(addon.ManifestId, selected.size());
            if (inserted)
            {
                selected.push_back(index);
                continue;
            }
            if (!addon.IsGlobal && addons[selected[slot->second]].IsGlobal)
            {
                selected[slot->second] = index;
            }
        }
        return selected;
    }
} // namespace HaloDesktop::Services
