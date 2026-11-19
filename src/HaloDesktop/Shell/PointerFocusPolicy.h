#pragma once

namespace HaloDesktop::Shell
{
    // What the shell knows about a pointer press on the content area, read from
    // the press's original source and the element holding focus at that moment.
    struct ContentPressFacts final
    {
        // The press landed in a text input or anything inside one (its clear
        // button, its caret area, a suggestion it owns).
        bool LandedInTextInput{};
        // The pressed control declares that interacting with it does not move
        // focus (AllowFocusOnInteraction is off), such as a clear button drawn
        // beside a box that must keep the caret.
        bool LandedOnFocusKeepingControl{};
        // A text input holds the caret when the press starts.
        bool CaretInTextInput{};
    };

    struct ContentPressPlan final
    {
        // For the rest of this pointer interaction, refuse pointer focus moving
        // into a text input (see ShouldRefusePointerFocus).
        bool GuardTextFocus{};
        // Move the existing caret to the focus sink now.
        bool ParkCaret{};
    };

    [[nodiscard]] ContentPressPlan PlanContentPress(ContentPressFacts const& facts) noexcept;

    // A focus move the shell sees while a press may be in progress.
    struct FocusMoveFacts final
    {
        // A press that did not land in a text input is in progress (the guard
        // armed by PlanContentPress and not yet cleared by release, cancel or
        // capture loss).
        bool GuardActive{};
        // The move is the one XAML makes for a pointer interaction. Keyboard,
        // programmatic and automation moves never qualify.
        bool ByPointer{};
        // The new element is a text input or inside one.
        bool TargetIsTextInput{};
        // The new element lives in the shell's content area. A dialog, flyout,
        // popup, sheet or overlay is a separate tree the press never reached.
        bool TargetInShellContent{};
        // The same text input already holds focus, so the move takes nothing away.
        bool TargetAlreadyFocused{};
    };

    // XAML gives pointer focus to the page's first tab stop when a press lands on
    // something that cannot hold focus. On a page whose first tab stop is a text
    // box that puts a caret in the box wherever the viewer clicks. The rule is
    // deliberately about "a text input" only, never about a page, a control name
    // or a control type beyond that: it once named only Home's search box, and
    // every page rebuilt with another box brought the stuck caret back.
    [[nodiscard]] bool ShouldRefusePointerFocus(FocusMoveFacts const& facts) noexcept;
}
