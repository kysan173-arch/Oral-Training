# Generate search magnifier line icon (64x64 PNG, brand blue)
Add-Type -AssemblyName System.Drawing
$outDir = $PSScriptRoot
$blue = [System.Drawing.ColorTranslator]::FromHtml('#5B6EF5')
$bmp = New-Object System.Drawing.Bitmap 64, 64
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.Clear([System.Drawing.Color]::Transparent)
$pen = New-Object System.Drawing.Pen $blue, 4
$pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
$pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
# magnifier lens (circle)
$g.DrawArc($pen, 18, 18, 30, 30, 0, 360)
# magnifier handle (diagonal)
$g.DrawLine($pen, 42, 42, 54, 54)
$g.Dispose()
$pen.Dispose()
$p = Join-Path $outDir 'search.png'
$bmp.Save($p, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host "Generated $p"
