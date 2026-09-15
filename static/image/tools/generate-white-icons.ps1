# Generate white line icons for home quick-entry (64x64 PNG, white stroke)
Add-Type -AssemblyName System.Drawing
$outDir = Join-Path $PSScriptRoot "white"
New-Item -ItemType Directory -Path $outDir -Force | Out-Null
$white = [System.Drawing.Color]::White

function MakeCanvas() {
  $bmp = New-Object System.Drawing.Bitmap 64, 64
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
  $g.Clear([System.Drawing.Color]::Transparent)
  $pen = New-Object System.Drawing.Pen $white, 4
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

# train: lightning
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
  $g.DrawLine($p, 22, 48, 18, 54)
  $g.DrawLine($p, 18, 54, 28, 48)
}

# mistake: X
function DrawMistake($h) {
  $g = $h['g']; $p = $h['pen']
  $g.DrawLine($p, 18, 18, 46, 46)
  $g.DrawLine($p, 46, 18, 18, 46)
}

# growth: ascending bars
function DrawGrowth($h) {
  $g = $h['g']; $p = $h['pen']
  $g.DrawLine($p, 16, 48, 16, 38)
  $g.DrawLine($p, 32, 48, 32, 28)
  $g.DrawLine($p, 48, 48, 48, 18)
  $g.DrawLine($p, 12, 48, 52, 48)
}

$icons = @(
  @{ Name = 'train.png'; Fn = 'DrawTrain' }
  @{ Name = 'phrase.png'; Fn = 'DrawPhrase' }
  @{ Name = 'mistake.png'; Fn = 'DrawMistake' }
  @{ Name = 'growth.png'; Fn = 'DrawGrowth' }
)

foreach ($icon in $icons) {
  $c = MakeCanvas
  & $icon.Fn $c
  SavePng $c $icon.Name
}

Write-Host "Done: $outDir"
