# Generate filled tabbar icons (81x81 PNG)
# Usage: powershell -ExecutionPolicy Bypass -File generate-icons.ps1
Add-Type -AssemblyName System.Drawing
$outDir = $PSScriptRoot
$inactive = [System.Drawing.ColorTranslator]::FromHtml('#B8BFD0')
$active   = [System.Drawing.ColorTranslator]::FromHtml('#667EEA')

function MakeCanvas($color) {
  $bmp = New-Object System.Drawing.Bitmap 81, 81
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
  $g.Clear([System.Drawing.Color]::Transparent)
  $brush = New-Object System.Drawing.SolidBrush $color
  $h = @{}
  $h['bmp'] = $bmp
  $h['g'] = $g
  $h['brush'] = $brush
  $h
}

function SavePng($h, $name) {
  $h['g'].Dispose()
  $h['brush'].Dispose()
  $p = Join-Path $outDir $name
  $h['bmp'].Save($p, [System.Drawing.Imaging.ImageFormat]::Png)
  $h['bmp'].Dispose()
  Write-Host "Generated $name"
}

# home: rounded house (roof + body)
function DrawHome($h) {
  $g = $h['g']; $b = $h['brush']
  $roof = New-Object System.Drawing.Drawing2D.GraphicsPath
  $roof.AddLine(20, 42, 40, 20)
  $roof.AddLine(40, 20, 60, 42)
  $roof.CloseFigure()
  $g.FillPath($b, $roof)
  $roof.Dispose()
  $body = New-Object System.Drawing.Drawing2D.GraphicsPath
  $body.AddArc(22, 40, 12, 12, 180, 90)
  $body.AddArc(46, 40, 12, 12, 270, 90)
  $body.AddArc(46, 50, 12, 12, 0, 90)
  $body.AddArc(22, 50, 12, 12, 90, 90)
  $body.CloseFigure()
  $g.FillPath($b, $body)
  $body.Dispose()
}

# training: chat bubbles (main + small)
function DrawTraining($h) {
  $g = $h['g']; $b = $h['brush']
  $main = New-Object System.Drawing.Drawing2D.GraphicsPath
  $main.AddArc(18, 28, 16, 16, 180, 90)
  $main.AddArc(42, 28, 16, 16, 270, 90)
  $main.AddArc(42, 42, 16, 16, 0, 90)
  $main.AddArc(18, 42, 16, 16, 90, 90)
  $main.AddLine(28, 58, 22, 64)
  $main.AddLine(22, 64, 34, 58)
  $main.CloseFigure()
  $g.FillPath($b, $main)
  $main.Dispose()
  $small = New-Object System.Drawing.Drawing2D.GraphicsPath
  $small.AddArc(48, 16, 16, 16, 180, 90)
  $small.AddArc(64, 16, 16, 16, 270, 90)
  $small.AddArc(64, 22, 16, 16, 0, 90)
  $small.AddArc(48, 22, 16, 16, 90, 90)
  $small.CloseFigure()
  $g.FillPath($b, $small)
  $small.Dispose()
}

# report: bar chart
function DrawReport($h) {
  $g = $h['g']; $b = $h['brush']
  $g.FillRectangle($b, 22, 30, 12, 30)
  $g.FillRectangle($b, 38, 18, 12, 42)
  $g.FillRectangle($b, 54, 36, 12, 24)
}

# mine: head + rounded-shoulder body
function DrawMine($h) {
  $g = $h['g']; $b = $h['brush']
  # head (perfect circle)
  $g.FillEllipse($b, 30, 13, 21, 21)
  # body: rounded rectangle (bottom closed, top rounded)
  $body = New-Object System.Drawing.Drawing2D.GraphicsPath
  $body.AddArc(16, 44, 14, 14, 180, 90)
  $body.AddArc(51, 44, 14, 14, 270, 90)
  $body.AddArc(51, 54, 14, 14, 0, 90)
  $body.AddArc(16, 54, 14, 14, 90, 90)
  $body.CloseFigure()
  $g.FillPath($b, $body)
  $body.Dispose()
}

$icons = @(
  @{ Name = 'home.png';             Fn = 'DrawHome';     Color = $inactive }
  @{ Name = 'home_selected.png';    Fn = 'DrawHome';     Color = $active }
  @{ Name = 'training.png';         Fn = 'DrawTraining'; Color = $inactive }
  @{ Name = 'training_selected.png';Fn = 'DrawTraining'; Color = $active }
  @{ Name = 'report.png';           Fn = 'DrawReport';   Color = $inactive }
  @{ Name = 'report_selected.png';  Fn = 'DrawReport';   Color = $active }
  @{ Name = 'mine.png';             Fn = 'DrawMine';     Color = $inactive }
  @{ Name = 'mine_selected.png';    Fn = 'DrawMine';     Color = $active }
)

foreach ($icon in $icons) {
  $c = MakeCanvas $icon.Color
  & $icon.Fn $c
  SavePng $c $icon.Name
}

Write-Host "Done: $outDir"
