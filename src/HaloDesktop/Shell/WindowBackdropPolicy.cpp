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
        // The luminosity values are the ones Windows uses for its own standard
        // acrylic in each theme; luminosity is what holds back the wallpaper's
        // colours, so it does most of the work. The tint then keeps the result
        // on the palette's ground colour rather than Windows' neutral grey.
        return lightTheme
            ? AcrylicStrength{ .TintOpacity = 0.8f, .LuminosityOpacity = 0.9f }
            : AcrylicStrength{ .TintOpacity = 0.8f, .LuminosityOpacity = 0.96f };
    }
}
