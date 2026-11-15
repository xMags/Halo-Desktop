#pragma once

// The ordered worker every mpv command goes through: the reference client's
// `apps/desktop/tests/callQueue.test.ts` mirrored, plus its shutdown.
void RunCallQueueTests();
