[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

# Renders the application icon and the package logos from the Halo mark's geometry:
# the same 64-unit drawing the app's HaloMark control and the reference client's
# HaloMark.tsx use. Two rounded bars form an H, and a ring tilted -16 degrees about
# (32, 34) passes behind the bars at the top and in front of them at the bottom, with
# the bars cut clear of the front half.
#
# The icon has no tile: it sits straight on the taskbar, so the ring is the accent
# blue rather than the theme's ink, which reads on light and dark taskbars alike.
# Bars #4CC2FF (the dark theme's accent), ring #0067C0.
#
# GDI+ fills regions without antialiasing, so every asset is drawn at eight times its
# size and scaled down with a prefiltering bicubic resample.

$assetsDirectory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\src\HaloDesktop\Assets'))

$barColor = [System.Drawing.Color]::FromArgb(255, 0x4C, 0xC2, 0xFF)
$ringColor = [System.Drawing.Color]::FromArgb(255, 0x00, 0x67, 0xC0)
$supersample = 8
# The mark fills 98% of its square, so the ring's round caps never touch the edge.
$markFill = 0.98

# At or below 24 pixels the ring is drawn heavier and stands closer to the bars, or
# it thins past a pixel and its clearance cuts the H into four pieces.
function Get-MarkWeight([double] $markPixels) {
    if ($markPixels -le 24) {
        return @{ RingWidth = 7.0; Clearance = 3.0 }
    }
    return @{ RingWidth = 5.0; Clearance = 5.0 }
}

function New-RingHalf([bool] $front) {
    # SVG "M5 34 A27 9.5 0 0 1 59 34" (back) and "... 0 0 0 59 34" (front): the
    # ellipse centred on (32, 34), from its left end over the top or under the bottom.
    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $sweep = if ($front) { -180.0 } else { 180.0 }
    $path.AddArc([System.Drawing.RectangleF]::new(5.0, 24.5, 54.0, 19.0), 180.0, $sweep)
    $tilt = [System.Drawing.Drawing2D.Matrix]::new()
    $tilt.RotateAt(-16.0, [System.Drawing.PointF]::new(32.0, 34.0))
    $path.Transform($tilt)
    $tilt.Dispose()
    return $path
}

function New-Bars {
    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    foreach ($left in 13.0, 39.0) {
        # 12 wide, 50 tall, rx 6: fully rounded ends.
        $path.StartFigure()
        $path.AddArc([System.Drawing.RectangleF]::new($left, 7.0, 12.0, 12.0), 180.0, 180.0)
        $path.AddArc([System.Drawing.RectangleF]::new($left, 45.0, 12.0, 12.0), 0.0, 180.0)
        $path.CloseFigure()
    }
    return $path
}

# Draws the mark into a square of $markPixels at ($left, $top) on $graphics, whose
# pixels are $scale times the final asset's.
function Draw-Mark($graphics, [double] $left, [double] $top, [double] $markPixels, [double] $scale) {
    $weight = Get-MarkWeight $markPixels
    $unit = $markPixels * $markFill * $scale / 64.0
    $inset = $markPixels * (1.0 - $markFill) / 2.0 * $scale
    $state = $graphics.Save()
    $graphics.TranslateTransform([single]($left * $scale + $inset), [single]($top * $scale + $inset))
    $graphics.ScaleTransform([single]$unit, [single]$unit)

    $back = New-RingHalf $false
    $front = New-RingHalf $true
    $bars = New-Bars
    $ringPen = [System.Drawing.Pen]::new($ringColor, [single]$weight.RingWidth)
    $ringPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $ringPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $bandPen = [System.Drawing.Pen]::new([System.Drawing.Color]::Black, [single]($weight.RingWidth + $weight.Clearance))
    $bandPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $bandPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $band = $front.Clone()
    $barBrush = [System.Drawing.SolidBrush]::new($barColor)
    $region = $null
    try {
        $graphics.DrawPath($ringPen, $back)
        $band.Widen($bandPen)
        $region = [System.Drawing.Region]::new($bars)
        $region.Exclude($band)
        $graphics.FillRegion($barBrush, $region)
        $graphics.DrawPath($ringPen, $front)
    }
    finally {
        if ($region) { $region.Dispose() }
        $barBrush.Dispose()
        $band.Dispose()
        $bandPen.Dispose()
        $ringPen.Dispose()
        $bars.Dispose()
        $front.Dispose()
        $back.Dispose()
        $graphics.Restore($state)
    }
}

# A transparent canvas of $Width x $Height with the mark centred at $MarkSize.
function New-MarkBitmap([int] $Width, [int] $Height, [int] $MarkSize) {
    if ($MarkSize -gt $Width -or $MarkSize -gt $Height) {
        throw "A $MarkSize px mark does not fit a $Width x $Height asset."
    }

    $large = [System.Drawing.Bitmap]::new(
        $Width * $supersample,
        $Height * $supersample,
        [System.Drawing.Imaging.PixelFormat]::Format32bppPArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($large)
    try {
        $graphics.Clear([System.Drawing.Color]::Transparent)
        $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $left = ($Width - $MarkSize) / 2.0
        $top = ($Height - $MarkSize) / 2.0
        Draw-Mark $graphics $left $top $MarkSize $supersample
    }
    finally {
        $graphics.Dispose()
    }

    $bitmap = [System.Drawing.Bitmap]::new($Width, $Height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.Clear([System.Drawing.Color]::Transparent)
        $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
        $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $attributes = [System.Drawing.Imaging.ImageAttributes]::new()
        try {
            # Clamp rather than wrap at the edges, so no colour bleeds in from the
            # opposite side of the canvas.
            $attributes.SetWrapMode([System.Drawing.Drawing2D.WrapMode]::TileFlipXY)
            $graphics.DrawImage(
                $large,
                [System.Drawing.Rectangle]::new(0, 0, $Width, $Height),
                0, 0, $large.Width, $large.Height,
                [System.Drawing.GraphicsUnit]::Pixel,
                $attributes)
        }
        finally {
            $attributes.Dispose()
        }
    }
    finally {
        $graphics.Dispose()
        $large.Dispose()
    }
    return $bitmap
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

    $bitmap = New-MarkBitmap $Width $Height $MarkSize
    try {
        $bitmap.Save((Join-Path $assetsDirectory $Name), [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $bitmap.Dispose()
    }
}

function New-HaloIcon {
    param(
        [Parameter(Mandatory)]
        [string] $Name,

        [Parameter(Mandatory)]
        [int[]] $Sizes
    )

    # Each frame is drawn at its own size rather than scaled from one master, so the
    # small frames get the heavier ring.
    $sorted = @($Sizes | Sort-Object)
    $frames = [System.Collections.Generic.List[byte[]]]::new()
    foreach ($size in $sorted) {
        $bitmap = New-MarkBitmap $size $size $size
        $stream = [System.IO.MemoryStream]::new()
        try {
            $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
            $frames.Add($stream.ToArray())
        }
        finally {
            $stream.Dispose()
            $bitmap.Dispose()
        }
    }

    # ICO container: a 6-byte ICONDIR, then one 16-byte ICONDIRENTRY per frame,
    # then the frame payloads. The frames are PNG rather than BMP, which Windows
    # has accepted at every size since Vista and which keeps the file small.
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

# The mark on its own, for the README.
New-HaloAsset -Name 'halo-mark.png' -Width 260 -Height 260 -MarkSize 260

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
