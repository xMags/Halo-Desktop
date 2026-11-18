#pragma once

namespace HaloDesktop::Shell
{
    // What the window frame is showing, as the navigation layers report it.
    struct ShellChromeInput final
    {
        // The shell frame's current page is a title page (Detail). True also while
        // the sources sheet or the player is drawn over it.
        bool TitlePageCurrent{};
        // The sign-in overlay is open.
        bool SignInOverlayOpen{};
        // The player overlay covers the window below the title bar.
        bool PlayerOverlayOpen{};
    };

    struct ShellChrome final
    {
        // The whole window (ground, title bar, pane, sheet, dialogs and menus)
        // paints the dark palette whatever the theme preference. A title page is
        // drawn on its art in both themes, so a light frame around it is wrong.
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
