#pragma once

// The Downloads page's rules, mirroring the reference client's
// `apps/desktop/tests/downloadsLogic.test.ts` and `downloadsPresentation.test.ts`
// (labels, progress, sizes, time left, the throughput window, the bulk pause,
// late list reads, rows, cards, filters, selection, the rate line, the chart,
// the summary and storage), plus the dynamic range a download keeps in its
// index (Dolby Vision reads DV; an index written before the field loads).
void RunDownloadsPageTests();
