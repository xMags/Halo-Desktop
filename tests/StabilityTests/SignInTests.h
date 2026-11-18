#pragma once

// The sign-in screen's rules, mirroring the reference client's
// `apps/desktop/tests/signIn.test.ts` (the typed server address, failed
// sign-ins as the screen tells them apart, the local form's errors), the
// steps of `auth/Login.tsx` (discovery never replacing an open form, the
// custom form's way back), the landing page of `src-tauri/src/oauth.rs`,
// the sessions kept for another built-in server (`api.ts`), and the sign-in
// add-on registry, which is empty in a public build.
void RunSignInTests();
