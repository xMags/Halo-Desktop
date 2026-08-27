#pragma once

#include <cstddef>
#include <span>

namespace HaloDesktop::ViewModels
{
    // Picks the settings rail entry matching the section the reader has reached.
    //
    // `sectionTops` holds each section's top edge measured from the top of the
    // viewport, in document order, so entries go negative once a section has
    // scrolled past. `anchor` is how far below the viewport top a section has to
    // reach before it counts as the one being read; anchoring on a band rather than
    // the very top edge stops the selection flickering between neighbours while a
    // section boundary sits on the edge. `atEnd` marks a view scrolled to the
    // bottom, where the last section wins even though it never reaches the anchor:
    // the content ran out before it could.
    //
    // Kept inline rather than split into a translation unit so it stays out of the
    // shared project files while download work is in flight there.
    [[nodiscard]] inline std::size_t ActiveSettingsSection(
        std::span<double const> sectionTops,
        double anchor,
        bool atEnd) noexcept
    {
        if (sectionTops.empty())
        {
            return 0;
        }
        if (atEnd)
        {
            return sectionTops.size() - 1;
        }
        std::size_t active = 0;
        for (std::size_t index = 0; index < sectionTops.size(); ++index)
        {
            if (sectionTops[index] <= anchor)
            {
                active = index;
            }
        }
        return active;
    }
} // namespace HaloDesktop::ViewModels
