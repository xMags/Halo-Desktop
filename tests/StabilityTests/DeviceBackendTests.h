#pragma once

// The on-device backend (Halo without an account): every case of the
// reference's packages/core/test/deviceBackend.test.ts, the document store on
// disk in a temporary folder, the downloads partition, and the backend
// selector. Never touches the app's own data folder or a real addon host.
void RunDeviceBackendTests();
