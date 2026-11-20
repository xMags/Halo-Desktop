# libmpv corresponding source

Halo redistributes `libmpv-2.dll` under the GNU Lesser General Public License,
version 2.1 or later. The licence text is in
`licenses/third-party/GNU-LGPL-2.1.txt`. This file records exactly which binary
is shipped and where its source comes from, so a recipient can obtain, rebuild,
and substitute it.

## Shipped binary

| Field | Value |
| --- | --- |
| Release | `2026-10-22-488fec0789` |
| Asset | `mpv-dev-lgpl-x86_64-20261022-git-488fec0789.7z` |
| Asset SHA-256 | `133f9ed88ba9db933bc0a0d0a6e3316cec4f9fc2c459a03d420a7fa2448d6a6a` |
| `libmpv-2.dll` SHA-256 | `84FA6B98FF800770D7CE32AAD8F767C04C2B624916E57844E83E9FE917064C2B` |
| mpv revision | `488fec0789e5026804ac8dd03fb749570416d48a` |
| Configuration | LGPL, no GPL components |

The DLL hash and size are also recorded in `external/manifest.json` and are
enforced on every build by `tools/Verify-Dependencies.ps1`.

## Sources

- mpv itself: <https://github.com/mpv-player/mpv> at revision
  `488fec0789e5026804ac8dd03fb749570416d48a`.
- The Windows build, including the pinned revisions of every bundled library and
  the exact build configuration:
  <https://github.com/zhongfly/mpv-winbuild> at release `2026-10-22-488fec0789`.
  That repository's workflow logs record the full dependency graph used for this
  release. Upstream removes its releases after about a month, so the same
  archives, byte for byte, are kept at
  <https://github.com/xMags/Halo-Desktop/releases/tag/libmpv-2026-10-22-488fec0789>.
- The cross-compilation recipe those builds use:
  <https://github.com/shinchiro/mpv-winbuild-cmake>.

## Relinking

Halo links libmpv dynamically through the generated import library
`external/mpv/lib/mpv.lib`, and loads `libmpv-2.dll` from the application folder
at run time. Replacing that DLL with another build of the same libmpv ABI is
sufficient to substitute a modified version; no part of Halo needs to be
rebuilt. Note that `tools/Verify-Dependencies.ps1` will report the substituted
DLL as a hash mismatch, which is the gate working as intended rather than a
restriction on modification.

## Why the LGPL asset specifically

The upstream release publishes both a GPL and an LGPL build under similar
names. Halo is not distributed under the GPL, so only the `-lgpl-` asset is
acceptable. Pinning by hash exists partly to make that choice unfalsifiable: an
accidental switch to the GPL build changes the hash and fails the gate.
