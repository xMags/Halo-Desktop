<p align="center">
  <img src="src/HaloDesktop/Assets/halo-mark.png" width="96" alt="Halo logo" />
</p>

<h1 align="center">Halo Desktop</h1>

<p align="center">
  <strong>A native Windows 11 client for Halo, built with C++/WinRT, WinUI 3, and a medically inadvisable amount of Windows API.</strong>
</p>

<p align="center">
  <img alt="Windows 11" src="https://img.shields.io/badge/Windows-11-0078D4?style=flat-square&logo=windows11&logoColor=white" />
  <img alt="C++" src="https://img.shields.io/badge/C%2B%2B-20-00599C?style=flat-square&logo=cplusplus&logoColor=white" />
  <img alt="WinUI 3" src="https://img.shields.io/badge/UI-WinUI%203-512BD4?style=flat-square" />
  <img alt="libmpv" src="https://img.shields.io/badge/playback-libmpv-691A99?style=flat-square" />
  <img alt="Architecture" src="https://img.shields.io/badge/architecture-x64-brightgreen?style=flat-square" />
  <img alt="Source available" src="https://img.shields.io/badge/license-source--available-C62828?style=flat-square" />
</p>

> [!NOTE]
> This is basically the same project as [cryguy/halo](https://github.com/cryguy/halo). I just had an itch in my three left-over brain cells to write the Windows desktop app in the most native way possible. Apparently those brain cells chose C++/WinRT, COM, child windows, and manual lifetime management instead of peace.
>
> **This repository does not include the Halo backend.** For the API, server setup, and the rest of the actual responsible engineering, go to the [original Halo repository](https://github.com/cryguy/halo) and use the backend from there.

## What is this thing?

Halo Desktop is a packaged Windows 11 client for a self-hosted Halo server. It is not Electron, not a WebView wearing a fake moustache, and not a website trapped inside an `.exe`.

It is a real WinUI 3 application with a native C++/WinRT shell and libmpv rendering into a native child window. It talks to the Halo API for catalogs, metadata, library state, watch progress, addons, settings, streams, and subtitles. Downloads stay on the Windows device and remain available offline.

The upstream project said native desktop apps would come later. One remaining brain cell took that personally.

## What currently works?

| Area | Current behavior |
| --- | --- |
| Home | Real addon catalogs, featured media, artwork, and Continue Watching |
| Search | Searches supported addon catalogs and groups results by source |
| Library | Synced movies and series with filtering and sorting |
| Details | Metadata, seasons, episodes, progress, watched state, and artwork |
| Sources | Fresh addon resolution, source ranking, quality filters, and technical metadata |
| Player | Native libmpv video, audio and subtitle tracks, seeking, speed, volume, resume, fullscreen, and up next |
| Subtitles | Embedded tracks, addon subtitles, language preferences, styling, delay, and offline sidecars |
| Downloads | Device-local transfers, pause and resume, progress, replacement safety, offline playback, and account isolation |
| Settings | Synced playback preferences, addon management, theme, health status, and account controls |
| Authentication | Local Halo accounts plus OIDC browser sign-in support |

## Native, because apparently I enjoy problems

The desktop client deliberately uses native platform pieces where they make sense:

- **C++/WinRT and WinUI 3** for the application, navigation, controls, and Windows integration.
- **libmpv** using its LGPL Windows build for playback of the formats browsers look at and quietly walk away from.
- **Windows App SDK** for the packaged desktop runtime.
- **DPAPI** for protected local session and download-request storage.
- **Native child-window video hosting** instead of piping frames through a browser or inventing a new GPU-shaped disaster.
- **Windows audio sessions** so Halo behaves like an actual Windows media app in the Volume Mixer.

No browser frontend is hidden in here. The only HTML involved is the kind GitHub uses to render this README and judge my life choices.

## Before building

You need:

- Windows 11.
- Developer Mode enabled for loose MSIX registration.
- Visual Studio 2026 with **Desktop development with C++** and **C++/WinRT**.
- PowerShell 7.
- Internet access for the pinned libmpv development payload.
- Node.js 22 and Corepack only if you want to run the upstream fixture backend.

The supported development target is **x64 Debug**.

## Bring your own backend

First, set up a Halo backend from [cryguy/halo](https://github.com/cryguy/halo). This repository contains the Windows client only. There is no surprise Node server under the sofa.

Then create the local, ignored server configuration:

```powershell
Copy-Item `
  ".\src\HaloDesktop\Config\ServerConfig.example.h" `
  ".\src\HaloDesktop\Config\ServerConfig.local.h"
```

Edit `ServerConfig.local.h` and set:

```cpp
HaloDesktop::Config::ServerBaseUrl
```

Use the server base URL without a trailing slash. The file is intentionally ignored by Git. Please keep real deployment addresses and private configuration out of commits, because the application already has enough opportunities for chaos.

The tracked example points to the local fixture server at `http://127.0.0.1:18790`.

## Restore, build, and launch

Run the following from this repository root in PowerShell:

```powershell
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msbuild = & $vswhere `
  -latest `
  -products * `
  -requires Microsoft.Component.MSBuild `
  -find "MSBuild\**\Bin\MSBuild.exe" |
  Select-Object -First 1

& $msbuild "Halo-Desktop.slnx" `
  -t:Restore `
  -p:RestorePackagesConfig=true `
  -p:Configuration=Debug `
  -p:Platform=x64

& ".\tools\fetch-mpv.ps1"

& $msbuild "Halo-Desktop.slnx" `
  -p:Configuration=Debug `
  -p:Platform=x64 `
  -m `
  -v:m

$manifest = Get-Item ".\x64\Debug\HaloDesktop\AppxManifest.xml"
Add-AppxPackage -Register $manifest.FullName -ForceApplicationShutdown

$package = Get-AppxPackage -Name "56fcb18b-d21c-4111-93fb-bef0ffa36c43"
Start-Process "shell:AppsFolder\$($package.PackageFamilyName)!App"
```

`tools/fetch-mpv.ps1` is safe to run again. It downloads the latest baseline x64 LGPL developer archive, creates the MSVC import library, and places the ignored payload under `external/mpv/`. The fetched release is recorded locally in `external/mpv/VERSION.txt`.

If you update `Assets/halo-mark.png`, regenerate the package artwork with:

```powershell
& ".\tools\make-app-assets.ps1"
```

## Local fixture workflow

For deterministic development, the original Halo repository provides a fixture mode. Run this from a checkout of the upstream backend repository:

```powershell
corepack pnpm --filter @halo/api dev:fixtures
```

To test real playback and downloads, give the fixture a real media file:

```powershell
corepack pnpm --filter @halo/api dev:fixtures --media "C:\path\to\test.mkv"
```

The fixture listens on port `18790`, uses in-memory storage, and resets on restart. Its local test account is:

```text
Username: admin
Password: fixture-pass
```

Use an H.264 MKV or another libmpv-compatible file for playback checks. A file full of random bytes may exercise transfer logic, but libmpv will correctly identify it as nonsense, much like a compiler reviewing half of my decisions.

## Player shortcuts

| Key | Action |
| --- | --- |
| `Space` | Play or pause |
| `Left` / `Right` | Seek backward or forward 10 seconds |
| `Up` / `Down` | Change volume by 5 |
| `F` | Toggle fullscreen |
| `Escape` | Leave fullscreen first, then close the player |

Playback always starts from a freshly resolved addon source or a completed local download. Watch progress is reported while online. Completed downloads can include a subtitle sidecar for offline playback.

## Local data and security

Some stream providers return short-lived URLs or protected request headers. Those do not belong in UI bindings, logs, screenshots, or plain-text indexes.

Halo Desktop therefore keeps:

- Authentication state protected with Windows DPAPI.
- Download source URLs and headers inside per-download DPAPI-protected vault files.
- The ordinary download index free of raw source URLs and credentials.
- Download rows scoped to the signed-in server account.
- Old-account transfers paused and hidden after sign-out.
- Server configuration local and untracked.

In less formal terms: secrets go in the Windows vault-shaped hole, not in a `TextBlock`.

## Repository map

```text
Halo-Desktop/
|-- Halo-Desktop.slnx
|-- src/HaloDesktop/
|   |-- Api/                 HTTP client, DTOs, mapping, and error taxonomy
|   |-- Controls/            Reusable native XAML controls
|   |-- Models/              WinRT models shared with XAML
|   |-- Playback/            libmpv engine, subtitles, watch reporting, up next
|   |-- Security/            DPAPI helpers
|   |-- Services/            Auth, catalogs, library, downloads, settings, sources
|   |-- Shell/               Window, title bar, navigation, and presentation
|   |-- ViewModels/          Screen state and commands
|   `-- Views/               WinUI pages
|-- tools/                   Reproducible mpv and asset scripts
|-- external/mpv/            Ignored local mpv payload plus tracked instructions
|-- licenses/                End-user terms and third-party notices
|-- design/                  Local reference canvas, intentionally ignored
`-- packages/                Restored NuGet packages, intentionally ignored
```

The local `design/` reference stays outside public source. The configured deployment address stays outside tracked source too. Some things are private, even when the C++ compiler has already seen me at my weakest.

## License

Halo Desktop is **source available, not open source**. The source is published under the [Last Projects License 1.0](LICENSE).

In short, personal noncommercial study, building, running, forking, and modification are permitted subject to the full license. Organizational use, commercial use, private derivatives beyond the publication window, binary redistribution, and AI use require separate permission. The license text controls if this summary misses a detail.

Installed applications and official binaries are also governed by [Last Projects End-User Terms](licenses/TERMS.txt). Third-party components retain their own licenses, collected under [licenses/](licenses/THIRD-PARTY-NOTICES.txt).

For commercial or other permissions, contact `info@lastprojects.com`.

## Project status

This is an actively developed personal Windows client, not an official upstream desktop release. It currently targets Windows 11 x64 Debug builds and expects a separately running Halo backend.

Bug reports with exact reproduction steps are welcome. Code contributions are accepted only after a separate Contributor License Agreement is arranged, as required by the license. Reports containing only "it broke" will be forwarded to the same three brain cells that started this project, and response times may vary.

## Credits

- [cryguy/halo](https://github.com/cryguy/halo) for the original Halo project, backend, shared protocol work, and the excellent idea that caused all of this.
- [Stremio](https://www.stremio.com/) and its addon ecosystem for the protocol and compatible providers.
- [mpv](https://mpv.io/) for playing the media formats that browsers keep putting in the "someone else's problem" folder.
- Microsoft for C++/WinRT, WinUI 3, the Windows App SDK, and enough lifetime rules to keep every remaining brain cell employed.
