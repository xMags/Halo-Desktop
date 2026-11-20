#pragma once

namespace HaloDesktop::Shell
{
    // What the window frame is showing, as the navigation layers report it.
    struct ShellChromeInput final
    {
        // The sign-in overlay is open.
        bool SignInOverlayOpen{};
        // The player overlay covers the window below the title bar.
        bool PlayerOverlayOpen{};
    };

    struct ShellChrome final
    {
        // The whole window (ground, title bar, pane, sheet, dialogs and menus)
        // paints the dark palette whatever the theme preference: only for the
        // full-window sign-in screen. A title page is dark on its art in both
        // themes, but asks for that itself (DetailPage's RequestedTheme), so the
        // rail and title bar around it keep the viewer's theme.
        bool ForceDarkPalette{};
        // The title bar sits on the player's near-black ground with dark-palette
        // ink and light caption buttons, in both themes.
        bool PlayerTitleBar{};
        // The overlay spans the title bar row too, drawing under the title bar:
        // the sign-in screen is full window, its art behind the caption.
        bool OverlayCoversTitleBar{};
    };

    // The sign-in screen is dark in both themes, like the player: the art is the
    // point, and its glass is drawn for a dark ground.
    [[nodiscard]] ShellChrome ResolveShellChrome(ShellChromeInput const& input) noexcept;
}
