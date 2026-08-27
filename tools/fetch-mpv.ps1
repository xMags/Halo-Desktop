<#
.SYNOPSIS
    Restores the pinned libmpv developer payload into external\mpv.

.DESCRIPTION
    Halo redistributes libmpv-2.dll, so the payload is pinned to one upstream
    release asset by SHA-256. The release tag and asset name only locate the
    file; the hash decides whether the bytes are accepted. A re-tagged,
    replaced, or tampered asset therefore fails loudly instead of silently
    changing what Halo ships.

    Bumping libmpv is a deliberate act: update the three pinned values below,
    run this script, then copy the printed hash and size into
    external\manifest.json.
#>
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$releaseTag = '2026-08-23-9f9f8c4dd4'
$assetName = 'mpv-dev-lgpl-x86_64-20260823-git-9f9f8c4dd4.7z'
$assetSha256 = '16B9AEAEF838A79C61D0299E410AB45604ECD6591A17DA7ECF7AEF6A6FDD1C17'

$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$externalRoot = Join-Path $repositoryRoot 'external\mpv'
$temporaryRoot = Join-Path $env:TEMP ("HaloDesktop-mpv-" + [guid]::NewGuid().ToString('N'))

function Assert-GeneratedPath {
    param(
        [Parameter(Mandatory)]
        [string] $Path
    )

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $expectedPrefix = [System.IO.Path]::GetFullPath($externalRoot) + [System.IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith($expectedPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to modify a path outside external\mpv: $fullPath"
    }
}

function Get-VisualStudioTool {
    param(
        [Parameter(Mandatory)]
        [string] $Name
    )

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw 'Visual Studio Installer vswhere.exe was not found.'
    }

    $installationPath = & $vswhere -latest -property installationPath
    if (-not $installationPath) {
        throw 'No Visual Studio installation was found.'
    }

    $tool = Get-ChildItem -LiteralPath "$installationPath\VC\Tools\MSVC" -Directory |
        ForEach-Object { Join-Path $_.FullName "bin\Hostx64\x64\$Name" } |
        Where-Object { Test-Path -LiteralPath $_ } |
        Sort-Object -Descending |
        Select-Object -First 1
    if (-not $tool) {
        throw "$Name was not found in the latest Visual Studio installation."
    }

    return $tool
}

function New-DefinitionFile {
    param(
        [Parameter(Mandatory)]
        [string] $DllPath,
        [Parameter(Mandatory)]
        [string] $DefinitionPath
    )

    $dumpbin = Get-VisualStudioTool -Name 'dumpbin.exe'
    $exports = & $dumpbin /nologo /exports $DllPath
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin failed with exit code $LASTEXITCODE."
    }

    $symbols = foreach ($line in $exports) {
        if ($line -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)\s*$') {
            $Matches[1]
        }
    }
    if (-not $symbols) {
        throw 'No exports were found in libmpv-2.dll.'
    }

    @('LIBRARY libmpv-2.dll', 'EXPORTS') + ($symbols | Sort-Object -Unique) |
        Set-Content -LiteralPath $DefinitionPath -Encoding ascii
}

try {
    New-Item -ItemType Directory -Path $temporaryRoot -Force | Out-Null
    $archivePath = Join-Path $temporaryRoot $assetName
    $downloadUrl = "https://github.com/zhongfly/mpv-winbuild/releases/download/$releaseTag/$assetName"

    Write-Host "Downloading pinned $assetName from release $releaseTag..." -ForegroundColor White
    Invoke-WebRequest -Uri $downloadUrl -OutFile $archivePath

    $actualAssetHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
    if ($actualAssetHash -ne $assetSha256) {
        throw "The pinned libmpv archive does not match its expected hash. Expected $assetSha256, found $actualAssetHash."
    }
    Write-Host 'Archive hash verified.' -ForegroundColor White

    # tar.exe on Windows is bsdtar, which reads the 7-Zip container directly.
    & tar.exe -xf $archivePath -C $temporaryRoot
    if ($LASTEXITCODE -ne 0) {
        throw "tar.exe failed with exit code $LASTEXITCODE."
    }

    $clientHeader = Get-ChildItem -LiteralPath $temporaryRoot -Recurse -File -Filter 'client.h' |
        Where-Object { $_.Directory.Name -eq 'mpv' } |
        Select-Object -First 1
    $dll = Get-ChildItem -LiteralPath $temporaryRoot -Recurse -File -Filter 'libmpv-2.dll' |
        Select-Object -First 1
    $definition = Get-ChildItem -LiteralPath $temporaryRoot -Recurse -File -Filter '*.def' |
        Where-Object { $_.Name -match 'mpv' } |
        Select-Object -First 1
    if (-not $clientHeader -or -not $dll) {
        throw 'The developer archive did not contain include\mpv\client.h and libmpv-2.dll.'
    }

    $includeRoot = Join-Path $externalRoot 'include'
    $binaryRoot = Join-Path $externalRoot 'bin'
    $libraryRoot = Join-Path $externalRoot 'lib'
    foreach ($path in @($includeRoot, $binaryRoot, $libraryRoot)) {
        Assert-GeneratedPath -Path $path
        if (Test-Path -LiteralPath $path) {
            Remove-Item -LiteralPath $path -Recurse -Force
        }
        New-Item -ItemType Directory -Path $path -Force | Out-Null
    }

    Copy-Item -LiteralPath $clientHeader.Directory.FullName -Destination $includeRoot -Recurse
    $installedDll = Join-Path $binaryRoot 'libmpv-2.dll'
    Copy-Item -LiteralPath $dll.FullName -Destination $installedDll

    $definitionPath = Join-Path $externalRoot 'mpv.def'
    Assert-GeneratedPath -Path $definitionPath
    if ($definition) {
        Copy-Item -LiteralPath $definition.FullName -Destination $definitionPath -Force
    }
    else {
        New-DefinitionFile -DllPath $installedDll -DefinitionPath $definitionPath
    }

    $lib = Get-VisualStudioTool -Name 'lib.exe'
    $libraryPath = Join-Path $libraryRoot 'mpv.lib'
    & $lib /nologo "/def:$definitionPath" /name:libmpv-2.dll /machine:x64 "/out:$libraryPath"
    if ($LASTEXITCODE -ne 0) {
        throw "lib.exe failed with exit code $LASTEXITCODE."
    }
    $exportPath = [System.IO.Path]::ChangeExtension($libraryPath, '.exp')
    if (Test-Path -LiteralPath $exportPath) {
        Remove-Item -LiteralPath $exportPath -Force
    }

    # The upstream developer archive carries no legal text of its own, so the
    # LGPL notice and the corresponding-source pointer are maintained in the
    # repository instead: licenses\third-party\GNU-LGPL-2.1.txt and
    # external\mpv\CORRESPONDING-SOURCE.md.
    Set-Content -LiteralPath (Join-Path $externalRoot 'VERSION.txt') -Value $releaseTag -Encoding ascii

    $dllHash = (Get-FileHash -LiteralPath $installedDll -Algorithm SHA256).Hash
    $dllSize = (Get-Item -LiteralPath $installedDll).Length
    Write-Host ''
    Write-Host "libmpv $releaseTag is ready in external\mpv." -ForegroundColor Green
    Write-Host "  sha256: $dllHash" -ForegroundColor White
    Write-Host "  size:   $dllSize" -ForegroundColor White
    Write-Host 'If you changed the pin, copy these into external\manifest.json.' -ForegroundColor White
}
finally {
    if (Test-Path -LiteralPath $temporaryRoot) {
        $resolvedTemporaryRoot = [System.IO.Path]::GetFullPath($temporaryRoot)
        $expectedTemporaryPrefix = [System.IO.Path]::GetFullPath($env:TEMP) + [System.IO.Path]::DirectorySeparatorChar
        if ($resolvedTemporaryRoot.StartsWith($expectedTemporaryPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            Remove-Item -LiteralPath $resolvedTemporaryRoot -Recurse -Force
        }
    }
}
