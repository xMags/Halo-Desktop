# libmpv corresponding source

Halo redistributes `libmpv-2.dll` under the GNU Lesser General Public License,
version 2.1 or later. The licence text is in
`licenses/third-party/GNU-LGPL-2.1.txt`. This file records exactly which binary
is shipped and where its source comes from, so a recipient can obtain, rebuild,
and substitute it.

## Shipped binary

| Field | Value |
| --- | --- |
| Release | `2026-08-23-9f9f8c4dd4` |
| Asset | `mpv-dev-lgpl-x86_64-20260823-git-9f9f8c4dd4.7z` |
| Asset SHA-256 | `16b9aeaef838a79c61d0299e410ab45604ecd6591a17da7ecf7aef6a6fdd1c17` |
| `libmpv-2.dll` SHA-256 | `E68C30FC040ACF60AC4BD353D773D7AACE672E4DC07DB14371137B7444418F52` |
| mpv revision | `9f9f8c4dd4d82077b42879a738748fab1d5038c2` |
| Configuration | LGPL, no GPL components |

The DLL hash and size are also recorded in `external/manifest.json` and are
enforced on every build by `tools/Verify-Dependencies.ps1`.

## Sources

- mpv itself: <https://github.com/mpv-player/mpv> at revision
  `9f9f8c4dd4d82077b42879a738748fab1d5038c2`.
- The Windows build, including the pinned revisions of every bundled library and
  the exact build configuration:
  <https://github.com/zhongfly/mpv-winbuild> at release `2026-08-23-9f9f8c4dd4`.
  That repository's workflow logs record the full dependency graph used for this
  release.
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
