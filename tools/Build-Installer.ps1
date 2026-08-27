<#
.SYNOPSIS
    Produces the Halo installer from a verified Release build.

.DESCRIPTION
    Runs the whole release path in order so an installer cannot be produced
    from an output that was never checked:

      restore packages -> restore pinned native payloads -> build Release x64
      -> verify payload hashes and dependency closure -> compile the installer

    The dependency gate is the important step. It proves the folder about to be
    packaged has no dependency outside itself and Windows, which is what makes
    the installer safe to run on a machine that has no Visual C++
    redistributable, no Windows App SDK runtime, and no GPU driver supplying a
    Vulkan loader.

    The installer is unsigned, so Windows SmartScreen will warn about an
    unknown publisher on first run. That is expected.

.PARAMETER Version
    Installer version, default 1.0.0. Also stamped into the setup executable's
    file version resource.

.PARAMETER SkipBuild
    Reuse the existing Release output instead of rebuilding. The verification
    gate still runs, so this only skips compilation, never the checks.
#>
[CmdletBinding()]
param(
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string] $Version = '1.0.0',
    [switch] $SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$releaseOutput = Join-Path $repositoryRoot 'x64\Release\HaloDesktop'
$installerScript = Join-Path $repositoryRoot 'installer\HaloDesktop.iss'
$outputDirectory = Join-Path $repositoryRoot 'installer\Output'

function Get-MSBuild {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw 'Visual Studio Installer vswhere.exe was not found.'
    }

    $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' |
        Select-Object -First 1
    if (-not $msbuild) {
        throw 'MSBuild was not found in the latest Visual Studio installation.'
    }

    return $msbuild
}

function Get-InnoCompiler {
    # Inno Setup records its install location, which covers both the per-user
    # and the all-users installer without guessing at drive layout.
    $registryKeys = @(
        'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1',
        'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1',
        'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1'
    )
    foreach ($key in $registryKeys) {
        $location = (Get-ItemProperty -Path $key -Name 'InstallLocation' -ErrorAction SilentlyContinue).InstallLocation
        if ($location) {
            $candidate = Join-Path $location 'ISCC.exe'
            if (Test-Path -LiteralPath $candidate) {
                return $candidate
            }
        }
    }

    $fallbacks = @(
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe')
    )
    foreach ($candidate in $fallbacks) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    throw 'Inno Setup 6 was not found. Install it from https://jrsoftware.org/isdl.php and run this again.'
}

if (-not $SkipBuild) {
    $msbuild = Get-MSBuild

    Write-Host 'Restoring NuGet packages...' -ForegroundColor White
    & $msbuild (Join-Path $repositoryRoot 'Halo-Desktop.slnx') `
        -t:Restore -p:RestorePackagesConfig=true -p:Configuration=Release -p:Platform=x64 -v:minimal -nologo
    if ($LASTEXITCODE -ne 0) {
        throw "Package restore failed with exit code $LASTEXITCODE."
    }

    Write-Host 'Restoring pinned native payloads...' -ForegroundColor White
    & (Join-Path $PSScriptRoot 'fetch-mpv.ps1')
    & (Join-Path $PSScriptRoot 'Build-VulkanLoader.ps1')

    Write-Host 'Building Release x64...' -ForegroundColor White
    & $msbuild (Join-Path $repositoryRoot 'Halo-Desktop.slnx') `
        -p:Configuration=Release -p:Platform=x64 -m -v:minimal -nologo
    if ($LASTEXITCODE -ne 0) {
        throw "The Release build failed with exit code $LASTEXITCODE."
    }
}

if (-not (Test-Path -LiteralPath (Join-Path $releaseOutput 'HaloDesktop.exe'))) {
    throw "The Release output is missing: $releaseOutput"
}
# Without this index XAML cannot resolve ms-appx:/// and the app dies at
# startup, which is invisible until someone runs the installed copy.
if (-not (Test-Path -LiteralPath (Join-Path $releaseOutput 'resources.pri'))) {
    throw 'The Release output has no resources.pri, so the installed app would fail to start.'
}

Write-Host 'Verifying pinned payloads and dependency closure...' -ForegroundColor White
& (Join-Path $PSScriptRoot 'Verify-Dependencies.ps1') -OutputPath $releaseOutput
if ($LASTEXITCODE -ne 0) {
    throw 'Dependency verification failed, so no installer was produced.'
}

$iscc = Get-InnoCompiler
Write-Host "Compiling the installer with $iscc..." -ForegroundColor White
& $iscc "/DAppVersion=$Version" $installerScript
if ($LASTEXITCODE -ne 0) {
    throw "The Inno Setup compiler failed with exit code $LASTEXITCODE."
}

$setupPath = Join-Path $outputDirectory "HaloDesktop-$Version-Setup.exe"
if (-not (Test-Path -LiteralPath $setupPath)) {
    throw "The installer was not produced at $setupPath."
}

$hash = (Get-FileHash -LiteralPath $setupPath -Algorithm SHA256).Hash
$size = (Get-Item -LiteralPath $setupPath).Length
Write-Host ''
Write-Host 'Installer ready.' -ForegroundColor Green
Write-Host "  path:   $setupPath" -ForegroundColor White
Write-Host ("  size:   {0:N1} MB" -f ($size / 1MB)) -ForegroundColor White
Write-Host "  sha256: $hash" -ForegroundColor White
Write-Host ''
Write-Host 'The installer is unsigned, so SmartScreen will warn about an unknown publisher.' -ForegroundColor Yellow
