[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$externalRoot = Join-Path $repositoryRoot 'external\mpv'
$temporaryRoot = Join-Path $env:TEMP ("HaloDesktop-mpv-" + [guid]::NewGuid().ToString('N'))
$archivePath = $null

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
    $release = Invoke-RestMethod `
        -Uri 'https://api.github.com/repos/zhongfly/mpv-winbuild/releases/latest' `
        -Headers @{ 'User-Agent' = 'HaloDesktop-mpv-fetcher' }
    $asset = $release.assets |
        Where-Object { $_.name -match '^mpv-dev-lgpl-x86_64-\d' } |
        Select-Object -First 1
    if (-not $asset) {
        throw 'The latest release has no LGPL x64 libmpv developer archive.'
    }

    New-Item -ItemType Directory -Path $temporaryRoot -Force | Out-Null
    $archivePath = Join-Path $temporaryRoot $asset.name
    Write-Host "Downloading $($asset.name) from release $($release.tag_name)..." -ForegroundColor White
    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $archivePath

    Write-Host 'Extracting libmpv payload...' -ForegroundColor White
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
    Copy-Item -LiteralPath $dll.FullName -Destination (Join-Path $binaryRoot 'libmpv-2.dll')

    $definitionPath = Join-Path $externalRoot 'mpv.def'
    Assert-GeneratedPath -Path $definitionPath
    if ($definition) {
        Copy-Item -LiteralPath $definition.FullName -Destination $definitionPath -Force
    }
    else {
        New-DefinitionFile -DllPath (Join-Path $binaryRoot 'libmpv-2.dll') -DefinitionPath $definitionPath
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

    Set-Content -LiteralPath (Join-Path $externalRoot 'VERSION.txt') -Value $release.tag_name -Encoding ascii
    Write-Host "libmpv $($release.tag_name) is ready in external\mpv." -ForegroundColor Green
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
