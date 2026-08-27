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

function New-HaloIcon {
    param(
        [Parameter(Mandatory)]
        [string] $Name,

        [Parameter(Mandatory)]
        [int[]] $Sizes
    )

    $source = [System.Drawing.Image]::FromFile($sourcePath)
    $frames = [System.Collections.Generic.List[byte[]]]::new()

    try {
        foreach ($size in ($Sizes | Sort-Object)) {
            $bitmap = [System.Drawing.Bitmap]::new(
                $size,
                $size,
                [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
            $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
            $stream = [System.IO.MemoryStream]::new()
            try {
                $graphics.Clear([System.Drawing.Color]::Transparent)
                $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceOver
                $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
                $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
                $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
                $graphics.DrawImage($source, 0, 0, $size, $size)
                $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
                $frames.Add($stream.ToArray())
            }
            finally {
                $stream.Dispose()
                $graphics.Dispose()
                $bitmap.Dispose()
            }
        }
    }
    finally {
        $source.Dispose()
    }

    # ICO container: a 6-byte ICONDIR, then one 16-byte ICONDIRENTRY per frame,
    # then the frame payloads. The frames are PNG rather than BMP, which Windows
    # has accepted at every size since Vista and which keeps the file small.
    $sorted = $Sizes | Sort-Object
    $writer = [System.IO.BinaryWriter]::new([System.IO.File]::Create((Join-Path $assetsDirectory $Name)))
    try {
        $writer.Write([uint16]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]$frames.Count)

        $offset = 6 + (16 * $frames.Count)
        for ($index = 0; $index -lt $frames.Count; $index++) {
            $size = $sorted[$index]
            # 256 is stored as 0 because the field is a single byte.
            $writer.Write([byte]($size % 256))
            $writer.Write([byte]($size % 256))
            $writer.Write([byte]0)
            $writer.Write([byte]0)
            $writer.Write([uint16]1)
            $writer.Write([uint16]32)
            $writer.Write([uint32]$frames[$index].Length)
            $writer.Write([uint32]$offset)
            $offset += $frames[$index].Length
        }

        foreach ($frame in $frames) {
            $writer.Write($frame)
        }
    }
    finally {
        $writer.Dispose()
    }
}

New-HaloAsset -Name 'LockScreenLogo.scale-200.png' -Width 48 -Height 48 -MarkSize 38
New-HaloAsset -Name 'StoreLogo.png' -Width 50 -Height 50 -MarkSize 40
New-HaloAsset -Name 'Square44x44Logo.targetsize-24_altform-unplated.png' -Width 24 -Height 24 -MarkSize 20
New-HaloAsset -Name 'Square44x44Logo.scale-200.png' -Width 88 -Height 88 -MarkSize 68
New-HaloAsset -Name 'Square150x150Logo.scale-200.png' -Width 300 -Height 300 -MarkSize 220
New-HaloAsset -Name 'Wide310x150Logo.scale-200.png' -Width 620 -Height 300 -MarkSize 220
New-HaloAsset -Name 'SplashScreen.scale-200.png' -Width 1240 -Height 600 -MarkSize 240

# Used for the executable's own icon, the installer, and the shortcuts it
# creates. Explorer, the taskbar, and the Alt+Tab list each pick a different
# size out of this one file, so all of them are present.
New-HaloIcon -Name 'Halo.ico' -Sizes @(16, 20, 24, 32, 40, 48, 64, 128, 256)
