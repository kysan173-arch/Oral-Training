$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$envFile = Join-Path $root 'backend.env'
$exampleFile = Join-Path $root 'backend.env.example'

if (-not (Test-Path -LiteralPath $envFile)) {
  Copy-Item -LiteralPath $exampleFile -Destination $envFile
  Write-Host 'Created backend.env. Edit the database password and DeepSeek API Key, then run start-backend.cmd again.' -ForegroundColor Yellow
  exit 1
}

foreach ($line in Get-Content -LiteralPath $envFile) {
  $trimmed = $line.Trim()
  if (-not $trimmed -or $trimmed.StartsWith('#')) { continue }
  $parts = $trimmed.Split('=', 2)
  if ($parts.Count -ne 2) { throw "Invalid backend.env line: $line" }
  Set-Item -Path "Env:$($parts[0].Trim())" -Value $parts[1].Trim()
}

$env:PATH = "$root;$env:PATH"
Write-Host "Starting Oral Training API at http://$($env:BIND_ADDRESS):$($env:PORT)/api" -ForegroundColor Cyan
& (Join-Path $root 'oral_training_backend.exe')
exit $LASTEXITCODE
