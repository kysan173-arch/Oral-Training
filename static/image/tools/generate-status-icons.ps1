# Generate recent-training status line icons (48x48 PNG)
Add-Type -AssemblyName System.Drawing
$outDir = Join-Path $PSScriptRoot "status"
New-Item -ItemType Directory -Path $outDir -Force | Out-Null
$green = [System.Drawing.ColorTranslator]::FromHtml('#34C759')
$orange = [System.Drawing.ColorTranslator]::FromHtml('#FF9500')
$gray = [System.Drawing.ColorTranslator]::FromHtml('#A6A3AC')

function MakeCanvas($color) {
  $bmp = New-Object System.Drawing.Bitmap 48, 48
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
  $g.Clear([System.Drawing.Color]::Transparent)
  $pen = New-Object System.Drawing.Pen $color, 3.5
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

# done: checkmark
function DrawCheck($h) {
  $g = $h['g']; $p = $h['pen']
  $g.DrawLine($p, 14, 25, 21, 33)
  $g.DrawLine($p, 21, 33, 36, 14)
}

# in-progress: clock
function DrawClock($h) {
  $g = $h['g']; $p = $h['pen']
  $g.DrawArc($p, 13, 13, 22, 22, 0, 360)
  $g.DrawLine($p, 24, 18, 24, 25)
  $g.DrawLine($p, 24, 25, 30, 28)
}

# abandoned: cross
function DrawCross($h) {
  $g = $h['g']; $p = $h['pen']
  $g.DrawLine($p, 16, 16, 32, 32)
  $g.DrawLine($p, 32, 16, 16, 32)
}

$icons = @(
  @{ Name = 'ok.png'; Fn = 'DrawCheck'; Color = $green }
  @{ Name = 'warn.png'; Fn = 'DrawClock'; Color = $orange }
  @{ Name = 'muted.png'; Fn = 'DrawCross'; Color = $gray }
)

foreach ($icon in $icons) {
  $c = MakeCanvas $icon.Color
  & $icon.Fn $c
  SavePng $c $icon.Name
}

Write-Host "Done: $outDir"
