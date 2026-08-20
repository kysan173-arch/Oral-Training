# Generate tool line icons (64x64 PNG, stroke only, per-icon color)
Add-Type -AssemblyName System.Drawing
$outDir = $PSScriptRoot
$purple = [System.Drawing.ColorTranslator]::FromHtml('#5B6EF5')
$orange = [System.Drawing.ColorTranslator]::FromHtml('#FF9500')
$green  = [System.Drawing.ColorTranslator]::FromHtml('#34C759')
$red    = [System.Drawing.ColorTranslator]::FromHtml('#FF3B30')

function MakeCanvas($color) {
  $bmp = New-Object System.Drawing.Bitmap 64, 64
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
  $g.Clear([System.Drawing.Color]::Transparent)
  $pen = New-Object System.Drawing.Pen $color, 4
  $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
  $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
  $pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
  $h = @{}
  $h['bmp'] = $bmp
  $h['g'] = $g
  $h['pen'] = $pen
  $h
}

function SavePng($h, $name) {
  $h['g'].Dispose()
  $h['pen'].Dispose()
  $p = Join-Path $outDir $name
  $h['bmp'].Save($p, [System.Drawing.Imaging.ImageFormat]::Png)
  $h['bmp'].Dispose()
  Write-Host "Generated $name"
}

# growth: ascending bar chart (3 bars)
function DrawGrowth($h) {
  $g = $h['g']; $p = $h['pen']
  $g.DrawLine($p, 16, 48, 16, 38)
  $g.DrawLine($p, 32, 48, 32, 28)
  $g.DrawLine($p, 48, 48, 48, 18)
  $g.DrawLine($p, 12, 48, 52, 48)
}

# mistake: X mark
function DrawMistake($h) {
  $g = $h['g']; $p = $h['pen']
  $g.DrawLine($p, 18, 18, 46, 46)
  $g.DrawLine($p, 46, 18, 18, 46)
}

# phrase: speech bubble
function DrawPhrase($h) {
  $g = $h['g']; $p = $h['pen']
  $path = New-Object System.Drawing.Drawing2D.GraphicsPath
  $path.AddArc(14, 18, 16, 16, 180, 90)
  $path.AddArc(34, 18, 16, 16, 270, 90)
  $path.AddArc(34, 32, 16, 16, 0, 90)
  $path.AddArc(14, 32, 16, 16, 90, 90)
  $path.CloseFigure()
  $g.DrawPath($p, $path)
  $path.Dispose()
  # tail
  $g.DrawLine($p, 22, 48, 18, 54)
  $g.DrawLine($p, 18, 54, 28, 48)
}

# favorite: star
function DrawFavorite($h) {
  $g = $h['g']; $p = $h['pen']
  $cx = 32; $cy = 32; $outer = 22; $inner = 9
  $path = New-Object System.Drawing.Drawing2D.GraphicsPath
  $pts = New-Object System.Drawing.Point[] 10
  for ($i = 0; $i -lt 10; $i++) {
    $r = if ($i % 2 -eq 0) { $outer } else { $inner }
    $ang = (-90 + $i * 36) * [Math]::PI / 180
    $pts[$i] = New-Object System.Drawing.Point ([int]($cx + $r * [Math]::Cos($ang))), ([int]($cy + $r * [Math]::Sin($ang)))
  }
  $path.AddPolygon($pts)
  $path.CloseFigure()
  $g.DrawPath($p, $path)
  $path.Dispose()
}

# train: lightning / play (training action)
function DrawTrain($h) {
  $g = $h['g']; $p = $h['pen']
  $path = New-Object System.Drawing.Drawing2D.GraphicsPath
  $path.AddLine(34, 12, 20, 34)
  $path.AddLine(20, 34, 32, 34)
  $path.AddLine(32, 34, 28, 52)
  $path.AddLine(28, 52, 44, 30)
  $path.AddLine(44, 30, 32, 30)
  $path.AddLine(32, 30, 34, 12)
  $path.CloseFigure()
  $g.DrawPath($p, $path)
  $path.Dispose()
}

$icons = @(
  @{ Name = 'growth.png'; Fn = 'DrawGrowth'; Color = $purple }
  @{ Name = 'mistake.png'; Fn = 'DrawMistake'; Color = $red }
  @{ Name = 'phrase.png'; Fn = 'DrawPhrase'; Color = $green }
  @{ Name = 'favorite.png'; Fn = 'DrawFavorite'; Color = $orange }
  @{ Name = 'train.png'; Fn = 'DrawTrain'; Color = $purple }
)

foreach ($icon in $icons) {
  $c = MakeCanvas $icon.Color
  & $icon.Fn $c
  SavePng $c $icon.Name
}

Write-Host "Done: $outDir"
