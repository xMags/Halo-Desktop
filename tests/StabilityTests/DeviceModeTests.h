#pragma once

// Halo without an account, turned on: the move of this PC's data into an
// account (every case of the reference's apps/desktop/tests/deviceMove.test.ts
// plus its failure paths), the startup decision, the built-in addon top-up,
// the backend selector's switch, the downloads owner change (the reference's
// plan_adoption cases and an adoption on a temporary index) and the order the
// device store clears in. Fakes, temporary folders and nothing else.
void RunDeviceModeTests();
