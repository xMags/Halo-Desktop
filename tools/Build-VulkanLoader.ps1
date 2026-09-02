<#
.SYNOPSIS
    Builds the pinned Khronos Vulkan loader for one architecture.

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
param(
    [ValidateSet('x64', 'ARM64')]
    [string] $Platform = 'x64'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$sdkTag = 'vulkan-sdk-1.4.357.0'
$headersRevision = 'e3b1eec08173d6b825cd3ac88c885a63b621504a'
$loaderRevision = '5f157b62e333c63260d05d81bf66faa216ab0fb8'
$cmakeArchitecture = if ($Platform -eq 'ARM64') { 'ARM64' } else { 'x64' }
$architecture = if ($Platform -eq 'ARM64') { 'arm64' } else { 'x64' }
$manifestName = if ($Platform -eq 'ARM64') { 'external\manifest-arm64.json' } else { 'external\manifest.json' }

$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$sharedExternalRoot = Join-Path $repositoryRoot 'external\vulkan'
$externalRoot = if ($Platform -eq 'ARM64') {
    Join-Path $sharedExternalRoot 'arm64'
}
else {
    $sharedExternalRoot
}
$temporaryRoot = Join-Path $env:TEMP ("HaloDesktop-vulkan-$architecture-" + [guid]::NewGuid().ToString('N'))

function Assert-GeneratedPath {
    param(
        [Parameter(Mandatory)]
        [string] $Path
    )

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $expectedPrefix = [System.IO.Path]::GetFullPath($sharedExternalRoot) + [System.IO.Path]::DirectorySeparatorChar
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
        '-A', $cmakeArchitecture,
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
    # /Brepro replaces the PE timestamp with a hash of the contents, and
    # /DEBUG:NONE keeps the build directory's absolute path out of the debug
    # directory. Together they make the output depend only on the pinned
    # sources, so the recorded hash survives a rebuild.
    $configureArguments = @(
        '-S', $loaderSource,
        '-B', $loaderBuild,
        '-G', 'Visual Studio 18 2026',
        '-A', $cmakeArchitecture,
        '-DUPDATE_DEPS=OFF',
        '-DBUILD_TESTS=OFF',
        "-DVULKAN_HEADERS_INSTALL_DIR=$headersInstall",
        '-DCMAKE_C_FLAGS=/Brepro',
        '-DCMAKE_SHARED_LINKER_FLAGS=/Brepro /DEBUG:NONE',
        '-DCMAKE_EXE_LINKER_FLAGS=/Brepro /DEBUG:NONE'
    )
    if ($Platform -eq 'ARM64') {
        # Force the loader's supported cross-compilation path so it parses
        # generated assembly instead of executing an ARM64 helper on this x64 host.
        $configureArguments += '-DCMAKE_SYSTEM_NAME=Windows'
    }
    # ARMASM writes a probe object into its current directory while CMake checks
    # the compiler. Keep that generated object inside the disposable root.
    Push-Location -LiteralPath $temporaryRoot
    try {
        Invoke-Checked -Description 'cmake configure (loader)' -FilePath 'cmake' -Arguments $configureArguments
    }
    finally {
        Pop-Location
    }
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
    Write-Host "Vulkan loader $sdkTag for $Platform is ready in $externalRoot." -ForegroundColor Green
    Write-Host "  sha256: $loaderHash" -ForegroundColor White
    Write-Host "  size:   $loaderSize" -ForegroundColor White
    Write-Host "If you changed the pin, copy these into $manifestName." -ForegroundColor White
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
