# Halo

Halo is a native Windows desktop client for a personal media server. This prototype uses mock catalog, addon, stream, and download data while providing the foundation for local media playback in a later milestone.

## Prerequisites

- Visual Studio 2026 with the C++/WinRT workload
- Windows Developer Mode enabled for loose MSIX registration
- PowerShell

## Build and run

Run these commands from the repository root in PowerShell:

```powershell
$msbuild = "C:\Program Files\Microsoft Visual Studio\18\Professional\MSBuild\Current\Bin\MSBuild.exe"

& $msbuild "Halo-Desktop.slnx" -t:Restore -p:RestorePackagesConfig=true -p:Configuration=Debug -p:Platform=x64
& $msbuild "Halo-Desktop.slnx" -p:Configuration=Debug -p:Platform=x64 -m -v:m

$manifest = Get-Item "x64\Debug\HaloDesktop\AppxManifest.xml"

Add-AppxPackage -Register $manifest.FullName -ForceApplicationShutdown

$pfn = (Get-AppxPackage | Where-Object { $_.Name -eq '56fcb18b-d21c-4111-93fb-bef0ffa36c43' }).PackageFamilyName
Start-Process "shell:AppsFolder\$pfn!App"
```

The normal development configuration is x64 Debug.

## Repository layout

```text
Halo-Desktop/
|-- Halo-Desktop.slnx
|-- src/HaloDesktop/       WinUI 3 application project
|-- docs/HANDOFF.md        Milestone plan and implementation specification
|-- design/                Reference canvas, intentionally ignored by Git
`-- packages/              Restored NuGet packages, intentionally ignored by Git
```

The visual reference is stored in `design/`. The complete build plan and acceptance criteria are in `docs/HANDOFF.md`.
