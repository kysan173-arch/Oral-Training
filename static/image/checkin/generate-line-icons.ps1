# Generate checkin stat line icons (64x64 PNG, stroke only, per-icon color)
Add-Type -AssemblyName System.Drawing
$outDir = $PSScriptRoot
$purple = [System.Drawing.ColorTranslator]::FromHtml('#7C5CF0')
$green  = [System.Drawing.ColorTranslator]::FromHtml('#34C759')
$red    = [System.Drawing.ColorTranslator]::FromHtml('#FF3B30')

function MakeCanvas($color) {
  $bmp = New-Object System.Drawing.Bitmap 64, 64
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
  $g.Clear([System.Drawing.Color]::Transparent)
  $pen = New-Object System.Drawing.Pen $color, 4.5
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

# trophy: cup + handles + base + small star
function DrawTrophy($h) {
  $g = $h['g']; $p = $h['pen']
  # cup body
  $path = New-Object System.Drawing.Drawing2D.GraphicsPath
  $path.AddBezier(20, 18, 20, 10, 44, 10, 44, 18)
  $path.AddBezier(44, 18, 44, 28, 38, 30, 32, 30)
  $path.AddBezier(32, 30, 26, 30, 20, 28, 20, 18)
  $path.CloseFigure()
  $g.DrawPath($p, $path)
  $path.Dispose()
  # handles
  $g.DrawBezier($p, 20, 16, 12, 16, 12, 26, 20, 26)
  $g.DrawBezier($p, 44, 16, 52, 16, 52, 26, 44, 26)
  # base lines
  $g.DrawLine($p, 20, 36, 44, 36)
  $g.DrawLine($p, 26, 40, 38, 40)
  $g.DrawLine($p, 30, 44, 34, 44)
  # small star on cup
  $g.DrawLine($p, 32, 16, 32, 22)
  $g.DrawLine($p, 29, 19, 35, 19)
}

# check: big checkmark inside rounded square
function DrawCheck($h) {
  $g = $h['g']; $p = $h['pen']
  # rounded square outline
  $sq = New-Object System.Drawing.Drawing2D.GraphicsPath
  $sq.AddArc(16, 16, 12, 12, 180, 90)
  $sq.AddArc(36, 16, 12, 12, 270, 90)
  $sq.AddArc(36, 36, 12, 12, 0, 90)
  $sq.AddArc(16, 36, 12, 12, 90, 90)
  $sq.CloseFigure()
  $g.DrawPath($p, $sq)
  $sq.Dispose()
  # checkmark
  $g.DrawLine($p, 24, 32, 29, 38)
  $g.DrawLine($p, 29, 38, 41, 24)
}

# star: 5-point outline (bigger)
function DrawStar($h) {
  $g = $h['g']; $p = $h['pen']
  $cx = 32; $cy = 32; $outer = 24; $inner = 9
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

# heart: outline (bigger)
function DrawHeart($h) {
  $g = $h['g']; $p = $h['pen']
  $path = New-Object System.Drawing.Drawing2D.GraphicsPath
  $path.AddBezier(16, 24, 16, 13, 26, 10, 32, 20)
  $path.AddBezier(32, 20, 38, 10, 48, 13, 48, 24)
  $path.AddBezier(48, 24, 48, 35, 32, 48, 32, 48)
  $path.AddBezier(32, 48, 16, 35, 16, 24, 16, 24)
  $path.CloseFigure()
  $g.DrawPath($p, $path)
  $path.Dispose()
}

$icons = @(
  @{ Name = 'trophy.png'; Fn = 'DrawTrophy'; Color = $purple }
  @{ Name = 'star.png'; Fn = 'DrawStar'; Color = $purple }
  @{ Name = 'heart.png'; Fn = 'DrawHeart'; Color = $red }
  @{ Name = 'check.png'; Fn = 'DrawCheck'; Color = $green }
)

foreach ($icon in $icons) {
  $c = MakeCanvas $icon.Color
  & $icon.Fn $c
  SavePng $c $icon.Name
}

Write-Host "Done: $outDir"
