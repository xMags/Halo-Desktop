<#
.SYNOPSIS
    Verifies the pinned native payloads and the self-contained output folder.

.DESCRIPTION
    Two independent gates, both driven by external\manifest.json:

    1. Payload pinning. Every redistributed binary must match the manifest's
       SHA-256 and size. This is what catches a payload that drifted away from
       what the repository believes it ships, including a restore script whose
       pin was edited without refreshing the manifest.

    2. Dependency closure. Every import of every binary in the output folder
       must resolve either inside that folder or to a Windows component named in
       the manifest. Anything else means the app depends on something the target
       machine may not have, which is exactly how a self-contained build stops
       being self-contained. An unrecognised dependency fails rather than being
       assumed safe, so adding one is a deliberate, reviewable manifest edit.

    The Visual C++ runtime is rejected outright: the hybrid CRT in
    build\HybridCRT.props exists so no binary here needs it.

.PARAMETER OutputPath
    The built application folder to inspect. Defaults to the Release output.
    Only Release is distributable. A Debug folder is expected to fail this gate
    because it links the debug Universal CRT (ucrtbased.dll), which is a Windows
    SDK developer component and is not redistributable.

.PARAMETER SkipOutput
    Verify only the pinned payloads, for use before the app has been built.
#>
[CmdletBinding()]
param(
    [string] $OutputPath,
    [switch] $SkipOutput
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$manifestPath = Join-Path $repositoryRoot 'external\manifest.json'
if (-not $OutputPath) {
    $OutputPath = Join-Path $repositoryRoot 'x64\Release\HaloDesktop'
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

function Get-Dependencies {
    param(
        [Parameter(Mandatory)]
        [string] $DumpbinPath,
        [Parameter(Mandatory)]
        [string] $BinaryPath
    )

    $output = & $DumpbinPath /nologo /dependents $BinaryPath
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin failed on $BinaryPath with exit code $LASTEXITCODE."
    }

    foreach ($line in $output) {
        if ($line -match '^\s+([^\s]+\.dll)\s*$') {
            $Matches[1].ToLowerInvariant()
        }
    }
}

if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "The dependency manifest is missing: $manifestPath"
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.schemaVersion -ne 1) {
    throw "Unsupported manifest schema version $($manifest.schemaVersion)."
}
if ($manifest.architecture -ne 'x64') {
    throw "Unsupported manifest architecture $($manifest.architecture)."
}

$failures = [System.Collections.Generic.List[string]]::new()

foreach ($payload in $manifest.payloads) {
    $payloadPath = Join-Path $repositoryRoot $payload.path
    if (-not (Test-Path -LiteralPath $payloadPath -PathType Leaf)) {
        $failures.Add("$($payload.name): missing at $($payload.path). Run $($payload.restoreCommand).")
        continue
    }

    $actualHash = (Get-FileHash -LiteralPath $payloadPath -Algorithm SHA256).Hash
    $actualSize = (Get-Item -LiteralPath $payloadPath).Length
    if ($actualHash -ne $payload.sha256) {
        $failures.Add("$($payload.name): hash mismatch. Manifest $($payload.sha256), found $actualHash.")
    }
    if ($actualSize -ne $payload.size) {
        $failures.Add("$($payload.name): size mismatch. Manifest $($payload.size), found $actualSize.")
    }
}

if (-not $failures.Count) {
    Write-Host 'Pinned payloads match the manifest.' -ForegroundColor Green
    foreach ($payload in $manifest.payloads) {
        Write-Host "  $($payload.name) $($payload.version) [$($payload.license)]" -ForegroundColor White
    }
}

if (-not $SkipOutput) {
    if (-not (Test-Path -LiteralPath $OutputPath -PathType Container)) {
        throw "The application output folder was not found: $OutputPath"
    }

    $dumpbin = Get-VisualStudioTool -Name 'dumpbin.exe'
    $binaries = @(
        Get-ChildItem -LiteralPath $OutputPath -Recurse -File |
            Where-Object { $_.Extension -in '.exe', '.dll' }
    )
    if (-not $binaries) {
        throw "No binaries were found in $OutputPath."
    }

    $inOutput = @{}
    foreach ($binary in $binaries) {
        $inOutput[$binary.Name.ToLowerInvariant()] = $true
    }

    # Every payload must actually have been copied next to the executable.
    foreach ($payload in $manifest.payloads) {
        if (-not $inOutput.ContainsKey($payload.outputName.ToLowerInvariant())) {
            $failures.Add("$($payload.name): $($payload.outputName) was not copied into the output folder.")
        }
    }

    $allowed = @{}
    foreach ($name in $manifest.systemDependencies) {
        $allowed[$name.ToLowerInvariant()] = $true
    }
    $bannedPattern = '^(' + (($manifest.bannedDependencies | ForEach-Object { [regex]::Escape($_) }) -join '|') + ')'

    foreach ($binary in $binaries) {
        foreach ($dependency in (Get-Dependencies -DumpbinPath $dumpbin -BinaryPath $binary.FullName)) {
            if ($dependency -match $bannedPattern) {
                $failures.Add("$($binary.Name) imports the banned runtime $dependency.")
                continue
            }
            # API sets are resolved by the OS loader and have no file on disk.
            if ($dependency -match '^(api-ms-win-|ext-ms-win-)') {
                continue
            }
            if ($inOutput.ContainsKey($dependency)) {
                continue
            }
            if ($allowed.ContainsKey($dependency)) {
                continue
            }
            $failures.Add("$($binary.Name) depends on $dependency, which is neither in the output folder nor a known Windows component.")
        }
    }

    if (-not $failures.Count) {
        Write-Host "Dependency closure verified across $($binaries.Count) binaries in $OutputPath." -ForegroundColor Green
    }
}

if ($failures.Count) {
    Write-Host ''
    Write-Host 'Dependency verification failed:' -ForegroundColor Red
    foreach ($failure in ($failures | Sort-Object -Unique)) {
        Write-Host "  $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host ''
Write-Host 'Halo has no dependency outside its own folder and Windows itself.' -ForegroundColor Green
