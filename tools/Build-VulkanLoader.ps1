<#
.SYNOPSIS
    Builds the pinned Khronos Vulkan loader into external\vulkan.

.DESCRIPTION
    libmpv-2.dll carries a static import of vulkan-1.dll, so the loader must be
    present for the DLL to load at all, even though Halo renders through D3D11
    and never creates a Vulkan device. vulkan-1.dll is not a Windows component:
    it arrives with GPU drivers, so a clean machine, a fresh VM, or Windows
    Sandbox does not have it and Halo would fail to start playback there.

    Shipping the loader beside libmpv fixes that. The loader is a small shim
    that enumerates installed drivers and forwards calls; with no driver
    installed it simply reports none, mpv's Vulkan context creation fails, and
    playback continues on D3D11. Users who do have a driver keep working Vulkan.

    The loader is built from pinned Khronos sources rather than copied from an
    SDK installation so the binary Halo redistributes has provenance we control.
    Vulkan-Headers and Vulkan-Loader must stay on the same SDK tag.
#>
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$sdkTag = 'vulkan-sdk-1.4.357.0'
$headersRevision = 'e3b1eec08173d6b825cd3ac88c885a63b621504a'
$loaderRevision = '5f157b62e333c63260d05d81bf66faa216ab0fb8'

$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$externalRoot = Join-Path $repositoryRoot 'external\vulkan'
$temporaryRoot = Join-Path $env:TEMP ("HaloDesktop-vulkan-" + [guid]::NewGuid().ToString('N'))

function Assert-GeneratedPath {
    param(
        [Parameter(Mandatory)]
        [string] $Path
    )

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $expectedPrefix = [System.IO.Path]::GetFullPath($externalRoot) + [System.IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith($expectedPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to modify a path outside external\vulkan: $fullPath"
    }
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory)]
        [string] $Description,
        [Parameter(Mandatory)]
        [string] $FilePath,
        [Parameter(Mandatory)]
        [string[]] $Arguments
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE."
    }
}

function Get-PinnedRepository {
    param(
        [Parameter(Mandatory)]
        [string] $Url,
        [Parameter(Mandatory)]
        [string] $Revision,
        [Parameter(Mandatory)]
        [string] $Destination
    )

    Invoke-Checked -Description 'git clone' -FilePath 'git' -Arguments @(
        'clone', '--quiet', '--filter=blob:none', '--no-checkout', $Url, $Destination
    )
    Invoke-Checked -Description 'git checkout' -FilePath 'git' -Arguments @(
        '-C', $Destination, 'checkout', '--quiet', '--detach', $Revision
    )

    # A tag can be moved; the resolved commit is what actually pins the build.
    $head = & git -C $Destination rev-parse HEAD
    if ($LASTEXITCODE -ne 0) {
        throw "git rev-parse failed with exit code $LASTEXITCODE."
    }
    if ($head.Trim() -ne $Revision) {
        throw "$Url resolved to $($head.Trim()) instead of the pinned $Revision."
    }
}

try {
    foreach ($tool in @('git', 'cmake')) {
        if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
            throw "$tool was not found on PATH."
        }
    }

    $headersSource = Join-Path $temporaryRoot 'Vulkan-Headers'
    $headersInstall = Join-Path $temporaryRoot 'headers-install'
    $loaderSource = Join-Path $temporaryRoot 'Vulkan-Loader'
    $loaderBuild = Join-Path $temporaryRoot 'loader-build'
    New-Item -ItemType Directory -Path $temporaryRoot -Force | Out-Null

    Write-Host "Cloning pinned Vulkan-Headers ($sdkTag)..." -ForegroundColor White
    Get-PinnedRepository `
        -Url 'https://github.com/KhronosGroup/Vulkan-Headers.git' `
        -Revision $headersRevision `
        -Destination $headersSource

    Write-Host 'Installing Vulkan headers...' -ForegroundColor White
    Invoke-Checked -Description 'cmake configure (headers)' -FilePath 'cmake' -Arguments @(
        '-S', $headersSource,
        '-B', (Join-Path $temporaryRoot 'headers-build'),
        '-G', 'Visual Studio 18 2026',
        '-A', 'x64',
        "-DCMAKE_INSTALL_PREFIX=$headersInstall"
    )
    Invoke-Checked -Description 'cmake install (headers)' -FilePath 'cmake' -Arguments @(
        '--install', (Join-Path $temporaryRoot 'headers-build'), '--config', 'Release'
    )

    Write-Host "Cloning pinned Vulkan-Loader ($sdkTag)..." -ForegroundColor White
    Get-PinnedRepository `
        -Url 'https://github.com/KhronosGroup/Vulkan-Loader.git' `
        -Revision $loaderRevision `
        -Destination $loaderSource

    Write-Host 'Building vulkan-1.dll...' -ForegroundColor White
    Invoke-Checked -Description 'cmake configure (loader)' -FilePath 'cmake' -Arguments @(
        '-S', $loaderSource,
        '-B', $loaderBuild,
        '-G', 'Visual Studio 18 2026',
        '-A', 'x64',
        '-DUPDATE_DEPS=OFF',
        '-DBUILD_TESTS=OFF',
        "-DVULKAN_HEADERS_INSTALL_DIR=$headersInstall"
    )
    Invoke-Checked -Description 'cmake build (loader)' -FilePath 'cmake' -Arguments @(
        '--build', $loaderBuild, '--config', 'Release', '--parallel'
    )

    $builtLoader = Get-ChildItem -LiteralPath $loaderBuild -Recurse -File -Filter 'vulkan-1.dll' |
        Select-Object -First 1
    if (-not $builtLoader) {
        throw 'The Vulkan loader build did not produce vulkan-1.dll.'
    }

    $binaryRoot = Join-Path $externalRoot 'bin'
    Assert-GeneratedPath -Path $binaryRoot
    if (Test-Path -LiteralPath $binaryRoot) {
        Remove-Item -LiteralPath $binaryRoot -Recurse -Force
    }
    New-Item -ItemType Directory -Path $binaryRoot -Force | Out-Null

    $installedLoader = Join-Path $binaryRoot 'vulkan-1.dll'
    Copy-Item -LiteralPath $builtLoader.FullName -Destination $installedLoader -Force

    $licenseSource = Join-Path $loaderSource 'LICENSE.txt'
    if (Test-Path -LiteralPath $licenseSource) {
        Copy-Item `
            -LiteralPath $licenseSource `
            -Destination (Join-Path $repositoryRoot 'licenses\third-party\KhronosGroup.Vulkan-Loader--LICENSE.txt') `
            -Force
    }

    $versionPath = Join-Path $externalRoot 'VERSION.txt'
    Assert-GeneratedPath -Path $versionPath
    Set-Content -LiteralPath $versionPath -Value $sdkTag -Encoding ascii

    $loaderHash = (Get-FileHash -LiteralPath $installedLoader -Algorithm SHA256).Hash
    $loaderSize = (Get-Item -LiteralPath $installedLoader).Length
    Write-Host ''
    Write-Host "Vulkan loader $sdkTag is ready in external\vulkan." -ForegroundColor Green
    Write-Host "  sha256: $loaderHash" -ForegroundColor White
    Write-Host "  size:   $loaderSize" -ForegroundColor White
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
