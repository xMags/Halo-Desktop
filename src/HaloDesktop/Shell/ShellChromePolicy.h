#pragma once

namespace HaloDesktop::Shell
{
    // What the window frame is showing, as the navigation layers report it.
    struct ShellChromeInput final
    {
        // The shell frame's current page is a title page (Detail). True also while
        // the sources sheet or the player is drawn over it.
        bool TitlePageCurrent{};
        // The sign-in overlay covers the whole window.
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
    };

    // The sign-in page keeps the viewer's own theme even when the title page it
    // covers would force the dark one: it is not part of that page.
    [[nodiscard]] ShellChrome ResolveShellChrome(ShellChromeInput const& input) noexcept;
}
