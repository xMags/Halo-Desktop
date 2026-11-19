#include "Shell/WindowBackdropPolicy.h"

namespace HaloDesktop::Shell
{
    WindowBackdrop ResolveWindowBackdrop(WindowBackdropInput const& input) noexcept
    {
        auto const useAcrylic = input.PreferenceOn && input.AcrylicSupported;
        return WindowBackdrop{
            .UseAcrylic = useAcrylic,
            .PaintGround = !useAcrylic,
            .BlurLive = useAcrylic && input.WindowActive && !input.PlayerShowing,
        };
    }

    AcrylicStrength AcrylicStrengthFor(bool lightTheme) noexcept
    {
        // A light ground needs a little more of itself to stay as bright as the
        // palette's grey over a dark wallpaper.
        return lightTheme
            ? AcrylicStrength{ .TintOpacity = 0.78f, .LuminosityOpacity = 0.9f }
            : AcrylicStrength{ .TintOpacity = 0.8f, .LuminosityOpacity = 0.85f };
    }
}
