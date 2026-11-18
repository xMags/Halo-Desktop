#pragma once

// Direct addon resolution for Halo without an account: every case of the
// reference's packages/core/test/resolveMetaWithCast.test.ts, the protocol
// rules of addon/client.ts, normalize.ts, manifest.ts and nextVideo.ts, and
// the network fetcher's safety rules against a loopback server. Never
// contacts a real addon host.
void RunAddonResolveTests();
