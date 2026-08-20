# Generate data dashboard line icons (64x64 PNG, per-icon color)
Add-Type -AssemblyName System.Drawing
$outDir = $PSScriptRoot
New-Item -ItemType Directory -Path $outDir -Force | Out-Null
$blue = [System.Drawing.ColorTranslator]::FromHtml('#5B6EF5')
$green = [System.Drawing.ColorTranslator]::FromHtml('#34C759')
$purple = [System.Drawing.ColorTranslator]::FromHtml('#8B5CF6')
$orange = [System.Drawing.ColorTranslator]::FromHtml('#FF9500')

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

# students: person (head + shoulders)
function DrawStudents($h) {
  $g = $h['g']; $p = $h['pen']
  $g.DrawArc($p, 27, 10, 10, 10, 0, 360)
  $g.DrawArc($p, 14, 34, 36, 26, 0, 180)
}

# sessions: stacked books / play
function DrawSessions($h) {
  $g = $h['g']; $p = $h['pen']
  $g.DrawRectangle($p, 14, 18, 36, 12)
  $g.DrawLine($p, 14, 34, 50, 34)
  $g.DrawLine($p, 14, 34, 14, 48)
  $g.DrawLine($p, 50, 34, 50, 48)
  $g.DrawLine($p, 14, 48, 50, 48)
}

# score: star
function DrawScore($h) {
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

# pass rate: target (circle + center + crosshair)
function DrawTarget($h) {
  $g = $h['g']; $p = $h['pen']
  $g.DrawArc($p, 14, 14, 36, 36, 0, 360)
  $g.DrawArc($p, 24, 24, 16, 16, 0, 360)
  $g.DrawEllipse($p, 29, 29, 6, 6)
}

$icons = @(
  @{ Name = 'students.png'; Fn = 'DrawStudents'; Color = $blue }
  @{ Name = 'sessions.png'; Fn = 'DrawSessions'; Color = $green }
  @{ Name = 'score.png'; Fn = 'DrawScore'; Color = $purple }
  @{ Name = 'target.png'; Fn = 'DrawTarget'; Color = $orange }
)

foreach ($icon in $icons) {
  $c = MakeCanvas $icon.Color
  & $icon.Fn $c
  SavePng $c $icon.Name
}

Write-Host "Done: $outDir"
