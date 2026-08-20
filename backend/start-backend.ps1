$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$envFile = Join-Path $root "backend.env"
$exampleFile = Join-Path $root "backend.env.example"

# 1. Check config
if (-not (Test-Path -LiteralPath $envFile)) {
  Copy-Item -LiteralPath $exampleFile -Destination $envFile
  Write-Host "Created backend.env. Edit the database password and DeepSeek API Key, then run again." -ForegroundColor Yellow
  exit 1
}

# 2. Load environment
foreach ($line in Get-Content -LiteralPath $envFile) {
  $trimmed = $line.Trim()
  if (-not $trimmed -or $trimmed.StartsWith("#")) { continue }
  $parts = $trimmed.Split("=", 2)
  if ($parts.Count -ne 2) { throw "Invalid backend.env line: $line" }
  Set-Item -Path "Env:$($parts[0].Trim())" -Value $parts[1].Trim()
}

# 3. Ensure PostgreSQL running
Write-Host "[1/3] Checking PostgreSQL ..." -ForegroundColor Cyan
$pgService = Get-Service -Name "postgresql-x64-18" -ErrorAction SilentlyContinue
if (-not $pgService) {
  Write-Host "  WARNING: postgresql-x64-18 service not found. Is PostgreSQL installed?" -ForegroundColor Yellow
} elseif ($pgService.Status -ne "Running") {
  Write-Host "  PostgreSQL not running, starting..." -ForegroundColor Yellow
  Start-Service -Name "postgresql-x64-18"
  Start-Sleep -Seconds 2
  $pgService.Refresh()
  if ($pgService.Status -eq "Running") {
    Write-Host "  PostgreSQL started." -ForegroundColor Green
  } else {
    Write-Host "  ERROR: PostgreSQL failed to start!" -ForegroundColor Red
    exit 1
  }
} else {
  Write-Host "  PostgreSQL is already running." -ForegroundColor Green
}

# 4. Check backend executable
Write-Host "[2/3] Checking backend executable ..." -ForegroundColor Cyan
$exePath = Join-Path $root "oral_training_backend.exe"
if (-not (Test-Path $exePath)) {
  $buildExe = Join-Path $root "build-msvc\Release\oral_training_backend.exe"
  if (Test-Path $buildExe) {
    Write-Host "  Copying executable from build directory..." -ForegroundColor Yellow
    Copy-Item $buildExe $root
  } else {
    Write-Host "  ERROR: oral_training_backend.exe not found. Build the project first!" -ForegroundColor Red
    Write-Host "  Run: cmake --build $root\build-msvc --config Release" -ForegroundColor Yellow
    exit 1
  }
}

$libpq = Join-Path $root "libpq.dll"
if (-not (Test-Path $libpq)) {
  Write-Host "  ERROR: libpq.dll missing. Ensure all DLLs are in the backend directory." -ForegroundColor Red
  exit 1
}
Write-Host "  Backend executable ready." -ForegroundColor Green

# 5. Start backend
Write-Host "[3/3] Starting backend server ..." -ForegroundColor Cyan
$env:PATH = "$root;$env:PATH"
Write-Host "  http://$($env:BIND_ADDRESS):$($env:PORT)/api" -ForegroundColor White

# Kill stale processes
$oldProcs = Get-Process -Name "oral_training_backend" -ErrorAction SilentlyContinue
if ($oldProcs) {
  Write-Host "  Stopping stale process..." -ForegroundColor Yellow
  $oldProcs | Stop-Process -Force
  Start-Sleep -Seconds 1
}

& $exePath
exit $LASTEXITCODE
