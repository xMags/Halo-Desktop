#include "Shell/ShellChromePolicy.h"

namespace HaloDesktop::Shell
{
    ShellChrome ResolveShellChrome(ShellChromeInput const& input) noexcept
    {
        return ShellChrome{
            .ForceDarkPalette = input.SignInOverlayOpen,
            .PlayerTitleBar = input.PlayerOverlayOpen && !input.SignInOverlayOpen,
            .OverlayCoversTitleBar = input.SignInOverlayOpen,
        };
    }
}
