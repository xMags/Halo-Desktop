[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

$assetsDirectory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\src\HaloDesktop\Assets'))
$sourcePath = Join-Path $assetsDirectory 'halo-mark.png'
if (-not (Test-Path -LiteralPath $sourcePath)) {
    throw "Halo mark not found: $sourcePath"
}

function New-HaloAsset {
    param(
        [Parameter(Mandatory)]
        [string] $Name,

        [Parameter(Mandatory)]
        [int] $Width,

        [Parameter(Mandatory)]
        [int] $Height,

        [Parameter(Mandatory)]
        [int] $MarkSize
    )

    if ($MarkSize -gt $Width -or $MarkSize -gt $Height) {
        throw "The mark does not fit inside $Name."
    }

    $source = [System.Drawing.Image]::FromFile($sourcePath)
    $bitmap = [System.Drawing.Bitmap]::new(
        $Width,
        $Height,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)

    try {
        $graphics.Clear([System.Drawing.Color]::Transparent)
        $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceOver
        $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality

        $left = [int](($Width - $MarkSize) / 2)
        $top = [int](($Height - $MarkSize) / 2)
        $graphics.DrawImage($source, $left, $top, $MarkSize, $MarkSize)
        $bitmap.Save((Join-Path $assetsDirectory $Name), [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $graphics.Dispose()
        $bitmap.Dispose()
        $source.Dispose()
    }
}

New-HaloAsset -Name 'LockScreenLogo.scale-200.png' -Width 48 -Height 48 -MarkSize 38
New-HaloAsset -Name 'StoreLogo.png' -Width 50 -Height 50 -MarkSize 40
New-HaloAsset -Name 'Square44x44Logo.targetsize-24_altform-unplated.png' -Width 24 -Height 24 -MarkSize 20
New-HaloAsset -Name 'Square44x44Logo.scale-200.png' -Width 88 -Height 88 -MarkSize 68
New-HaloAsset -Name 'Square150x150Logo.scale-200.png' -Width 300 -Height 300 -MarkSize 220
New-HaloAsset -Name 'Wide310x150Logo.scale-200.png' -Width 620 -Height 300 -MarkSize 220
New-HaloAsset -Name 'SplashScreen.scale-200.png' -Width 1240 -Height 600 -MarkSize 240
