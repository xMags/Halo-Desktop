Run `tools/fetch-mpv.ps1` to produce the ignored libmpv headers, DLL, definition file, and import library.

The payload is pinned to one upstream release asset by SHA-256, not to a moving
"latest" tag. The release tag and asset name only locate the file; the hash
decides whether the bytes are accepted, so a replaced or tampered asset fails
loudly instead of silently changing what Halo ships. `VERSION.txt` records the
pinned release locally.

Halo redistributes this binary, so the LGPL variant is mandatory. See
`CORRESPONDING-SOURCE.md` for the exact revision and how to obtain the source.
`tools/Verify-Dependencies.ps1` enforces the pin against `external/manifest.json`.
