#include "Shell/WindowPlacementPolicy.h"

#include <algorithm>

namespace HaloDesktop::Shell
{
    namespace
    {
        // Enough of the title strip to see and to drag the window by.
        constexpr std::int64_t TitleStripHeight = 32;
        constexpr std::int64_t VisibleTitleWidth = 120;
        constexpr std::int64_t VisibleTitleHeight = 16;
        // Far beyond any real desktop: a value past this is not a saved window.
        constexpr std::int64_t CoordinateLimit = 100000;

        [[nodiscard]] std::int64_t Overlap(std::int64_t a0, std::int64_t a1, std::int64_t b0, std::int64_t b1) noexcept
        {
            return (std::max)(std::int64_t{ 0 }, (std::min)(a1, b1) - (std::max)(a0, b0));
        }

        [[nodiscard]] bool Plausible(WindowRect const& rect) noexcept
        {
            auto const within = [](std::int64_t value) noexcept { return value > -CoordinateLimit && value < CoordinateLimit; };
            return within(rect.Left) && within(rect.Top) && within(rect.Right) && within(rect.Bottom)
                && rect.Width() > 0 && rect.Height() > 0;
        }
    }

    bool SavesAsMaximized(bool maximized, bool minimized, bool restoresToMaximized) noexcept
    {
        return minimized ? restoresToMaximized : maximized;
    }

    std::optional<WindowRect> RestorableBounds(
        WindowRect const& saved,
        std::span<WindowRect const> workAreas,
        std::int32_t minimumWidth,
        std::int32_t minimumHeight) noexcept
    {
        if (!Plausible(saved) || minimumWidth <= 0 || minimumHeight <= 0)
        {
            return std::nullopt;
        }
        // The screen showing the most of the title strip is the one it is on.
        WindowRect const* home = nullptr;
        std::int64_t best = 0;
        for (auto const& area : workAreas)
        {
            if (!Plausible(area))
            {
                continue;
            }
            auto const width = Overlap(saved.Left, saved.Right, area.Left, area.Right);
            auto const height = Overlap(saved.Top, std::int64_t{ saved.Top } + TitleStripHeight, area.Top, area.Bottom);
            if (width >= (std::min)(VisibleTitleWidth, saved.Width()) && height >= VisibleTitleHeight && width * height > best)
            {
                best = width * height;
                home = &area;
            }
        }
        if (!home)
        {
            return std::nullopt;
        }

        // At least the minimum, at most the screen it is on.
        auto const width = std::clamp<std::int64_t>(saved.Width(), minimumWidth, (std::max)(std::int64_t{ minimumWidth }, home->Width()));
        auto const height = std::clamp<std::int64_t>(saved.Height(), minimumHeight, (std::max)(std::int64_t{ minimumHeight }, home->Height()));
        // Pulled back onto that screen only as far as the resize needs.
        auto const left = std::clamp<std::int64_t>(saved.Left, home->Left - width + VisibleTitleWidth, (std::max)(std::int64_t{ home->Left }, home->Right - VisibleTitleWidth));
        auto const top = std::clamp<std::int64_t>(saved.Top, home->Top, (std::max)(std::int64_t{ home->Top }, home->Bottom - TitleStripHeight));
        return WindowRect{
            static_cast<std::int32_t>(left),
            static_cast<std::int32_t>(top),
            static_cast<std::int32_t>(left + width),
            static_cast<std::int32_t>(top + height),
        };
    }
}
