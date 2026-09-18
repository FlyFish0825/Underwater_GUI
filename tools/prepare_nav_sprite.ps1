param(
    [string]$Source = (Join-Path $PSScriptRoot '..\resources\nav_animation\nav_sheet.png'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\resources\nav_animation\generated'),
    [string]$OffsetFile = (Join-Path $PSScriptRoot 'nav_sprite_offsets.json')
)

Add-Type -AssemblyName System.Drawing

$sourceBitmap = New-Object System.Drawing.Bitmap((Resolve-Path -LiteralPath $Source).Path)
try
{
    if ($sourceBitmap.Width -ne 1774 -or $sourceBitmap.Height -ne 887)
    {
        throw "Unexpected source size: $($sourceBitmap.Width)x$($sourceBitmap.Height); expected 1774x887."
    }

    $spec = Get-Content -LiteralPath $OffsetFile -Raw | ConvertFrom-Json
    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

    $frameWidth = 180
    $frameHeight = 150
    $frameCount = 8

    foreach ($row in $spec.rows)
    {
        $outputPath = Join-Path $OutputDirectory $row.output
        $outputBitmap = New-Object System.Drawing.Bitmap(
            ($frameWidth * $frameCount), $frameHeight,
            ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb))
        $graphics = [System.Drawing.Graphics]::FromImage($outputBitmap)
        try
        {
            $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
            $graphics.Clear([System.Drawing.Color]::Transparent)

            for ($frame = 0; $frame -lt $frameCount; $frame++)
            {
                $crop = $null
                try
                {
                    $x = [int]$spec.xCrop[$frame]
                    $y = [int]$row.yCrop
                    $dx = [int]$row.offsets[$frame][0]
                    $dy = [int]$row.offsets[$frame][1]
                    $sourceHeight = $frameHeight
                    $canvasY = 0
                    if ($row.PSObject.Properties.Name -contains 'sourceHeight')
                    {
                        $sourceHeight = [int]$row.sourceHeight
                    }
                    if ($row.PSObject.Properties.Name -contains 'canvasY')
                    {
                        $canvasY = [int]$row.canvasY
                    }
                    $crop = $sourceBitmap.Clone(
                        (New-Object System.Drawing.Rectangle($x, $y, $frameWidth, $sourceHeight)),
                        ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb))

                    # The destination frame cell is fixed. Offsets are integer translations only;
                    # clipping prevents an offset from changing the canvas dimensions.
                    $state = $graphics.Save()
                    try
                    {
                        $graphics.SetClip(
                            (New-Object System.Drawing.Rectangle(
                                ($frame * $frameWidth), 0, $frameWidth, $frameHeight)))
                        $graphics.DrawImageUnscaled(
                            $crop, ($frame * $frameWidth + $dx), ($canvasY + $dy))
                    }
                    finally
                    {
                        $graphics.Restore($state)
                    }
                }
                finally
                {
                    if ($null -ne $crop)
                    {
                        $crop.Dispose()
                    }
                }
            }

            $outputBitmap.Save($outputPath, [System.Drawing.Imaging.ImageFormat]::Png)
            Write-Output ("{0}: {1} ({2}x{3})" -f $row.name, $outputPath, $outputBitmap.Width, $outputBitmap.Height)
        }
        finally
        {
            $graphics.Dispose()
            $outputBitmap.Dispose()
        }
    }
}
finally
{
    $sourceBitmap.Dispose()
}
