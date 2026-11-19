#include "Shell/PointerFocusPolicy.h"

namespace HaloDesktop::Shell
{
    ContentPressPlan PlanContentPress(ContentPressFacts const& facts) noexcept
    {
        // A press in a text input is that input's business.
        if (facts.LandedInTextInput)
        {
            return {};
        }
        return ContentPressPlan{
            .GuardTextFocus = true,
            // A control that keeps focus where it is must not have the caret
            // taken from under it by the shell either.
            .ParkCaret = facts.CaretInTextInput && !facts.LandedOnFocusKeepingControl,
        };
    }

    bool ShouldRefusePointerFocus(FocusMoveFacts const& facts) noexcept
    {
        return facts.GuardActive &&
            facts.ByPointer &&
            facts.TargetIsTextInput &&
            facts.TargetInShellContent &&
            !facts.TargetAlreadyFocused;
    }
}
