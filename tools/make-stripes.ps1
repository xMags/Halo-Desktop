Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

function New-Stripes {
    param(
        [Parameter(Mandatory)]
        [string] $Path,

        [Parameter(Mandatory)]
        [string] $Base,

        [Parameter(Mandatory)]
        [string] $Stripe
    )

    $bitmap = [System.Drawing.Bitmap]::new(280, 420)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $brush = [System.Drawing.SolidBrush]::new([System.Drawing.ColorTranslator]::FromHtml($Stripe))

    try {
        $graphics.Clear([System.Drawing.ColorTranslator]::FromHtml($Base))

        for ($x = -420; $x -lt 700; $x += 14) {
            $points = [System.Drawing.Point[]] @(
                [System.Drawing.Point]::new($x, 0),
                [System.Drawing.Point]::new($x + 7, 0),
                [System.Drawing.Point]::new($x + 427, 420),
                [System.Drawing.Point]::new($x + 420, 420)
            )
            $graphics.FillPolygon($brush, $points)
        }

        $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $brush.Dispose()
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

$assetsDirectory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\src\HaloDesktop\Assets'))
New-Stripes -Path (Join-Path $assetsDirectory 'stripe-dark.png') -Base '#232428' -Stripe '#2A2B2F'
New-Stripes -Path (Join-Path $assetsDirectory 'stripe-light.png') -Base '#EFEFF2' -Stripe '#E4E4E8'
