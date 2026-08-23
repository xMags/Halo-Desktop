# Halo Desktop

Halo Desktop is a native Windows 11 media client built with C++/WinRT and WinUI 3. The current prototype uses mock server, catalog, source, addon, and download data. Local video playback is real and uses an embedded libmpv child window.

## Prerequisites

- Windows 11 with Developer Mode enabled for loose MSIX registration
- Visual Studio 2026 with the Desktop development with C++ and C++/WinRT components
- PowerShell
- Internet access when fetching the ignored libmpv development payload

The supported development target is x64 Debug.

## Restore, build, and run

Run these commands from the repository root in PowerShell:

```powershell
$msbuild = "C:\Program Files\Microsoft Visual Studio\18\Professional\MSBuild\Current\Bin\MSBuild.exe"

& $msbuild "Halo-Desktop.slnx" -t:Restore -p:RestorePackagesConfig=true -p:Configuration=Debug -p:Platform=x64
& ".\tools\fetch-mpv.ps1"
& $msbuild "Halo-Desktop.slnx" -p:Configuration=Debug -p:Platform=x64 -m -v:m

$manifest = Get-Item ".\x64\Debug\HaloDesktop\AppxManifest.xml"
Add-AppxPackage -Register $manifest.FullName -ForceApplicationShutdown

$package = Get-AppxPackage -Name "56fcb18b-d21c-4111-93fb-bef0ffa36c43"
Start-Process "shell:AppsFolder\$($package.PackageFamilyName)!App"
```

`tools/fetch-mpv.ps1` is safe to rerun. It downloads the baseline x64 developer archive, generates the MSVC import library, and writes the ignored payload under `external/mpv/`. Only `external/mpv/README.md` is tracked.

Run `tools/make-app-assets.ps1` after updating `Assets/halo-mark.png` to regenerate the MSIX icons, wide tile, and splash image without changing the logo artwork.

## Local playback

Any Play affordance opens the Player. The first Play action prompts for a local video file, and later actions reopen the remembered file when it still exists. Supported picker extensions are MKV, MP4, WebM, MOV, M4V, and AVI.

Player shortcuts:

- `Space`: play or pause
- `Left` and `Right`: seek backward or forward 10 seconds
- `Up` and `Down`: change volume by 5
- `F`: toggle fullscreen
- `Escape`: leave fullscreen first, then close the Player

Define `HALO_USE_NULL_PLAYBACK` for an x64 build when the simulated M8 engine is needed for debugging.

## Repository layout

```text
Halo-Desktop/
|-- Halo-Desktop.slnx
|-- src/HaloDesktop/       WinUI 3 application project
|-- tools/                 Reproducible asset and libmpv setup scripts
|-- external/mpv/          Tracked instructions plus ignored local payload
|-- docs/HANDOFF.md        Milestone specification and progress log
|-- design/                Reference canvas, intentionally ignored by Git
`-- packages/              Restored NuGet packages, intentionally ignored by Git
```

The reference canvas is read-only. See `docs/HANDOFF.md` for the complete prototype scope, architecture, acceptance criteria, and implementation record.
