#pragma once

// The unsent-progress outbox: the reference client's
// `apps/desktop/tests/watchOutbox.test.ts` mirrored, plus the file round trip, a
// corrupt or oversized file, the change of account, and a row staged again while
// its send was out.
void RunWatchOutboxTests();
