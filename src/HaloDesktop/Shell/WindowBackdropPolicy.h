#pragma once

namespace HaloDesktop::Shell
{
    // What the window knows when it decides how to paint its ground.
    struct WindowBackdropInput final
    {
        // The viewer's "Translucent window" setting on this PC.
        bool PreferenceOn{};
        // Windows can draw desktop acrylic for this window at all.
        bool AcrylicSupported{};
        // The window has input focus.
        bool WindowActive{};
        // The player covers the window.
        bool PlayerShowing{};
    };

    struct WindowBackdrop final
    {
        // The desktop acrylic backdrop is attached to the window.
        bool UseAcrylic{};
        // The window paints its own opaque ground over everything below it.
        // Exactly when there is no acrylic: with acrylic the ground has to be
        // see-through, and the backdrop's own fallback colour (the same ground
        // colour) covers every moment the blur is not drawn.
        bool PaintGround{};
        // The blur is drawn now. Off while the window is inactive, as Windows
        // draws every backdrop, and while the player covers the window: the
        // picture hides the ground anyway, and a blur nobody sees still costs
        // the compositor work on every frame of the video. Off shows the
        // backdrop's fallback colour.
        bool BlurLive{};
    };

    [[nodiscard]] WindowBackdrop ResolveWindowBackdrop(WindowBackdropInput const& input) noexcept;

    // How strongly the palette's ground colour is laid over the blurred
    // desktop. About as see-through as a standard Windows acrylic surface: the
    // desktop shows as a soft hint of colour, and the window still reads as
    // Halo's own colour with small grey text readable over a busy wallpaper.
    struct AcrylicStrength final
    {
        float TintOpacity{};
        float LuminosityOpacity{};
    };

    [[nodiscard]] AcrylicStrength AcrylicStrengthFor(bool lightTheme) noexcept;
}
