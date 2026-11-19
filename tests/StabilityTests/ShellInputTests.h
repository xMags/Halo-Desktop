#pragma once

// The two input rules that keep coming back: a caret that never leaves a text
// box (the shell's pointer focus guard) and the mouse wheel moving a sideways
// strip (the shared wheel guard, checked against every XAML file).
void RunShellInputTests();
