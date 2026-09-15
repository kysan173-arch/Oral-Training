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
Write-Host "[1/4] Checking PostgreSQL ..." -ForegroundColor Cyan
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

# 4. Stop any stale instance BEFORE touching the exe. Windows locks a running
#    executable, so refreshing the binary while the old server is still alive
#    fails with "being used by another process" and the rebuild would silently
#    never reach the server. Never let a failure here abort startup.
Write-Host "[2/4] Stopping any stale backend ..." -ForegroundColor Cyan
$port = $env:PORT
if (-not $port) { $port = "8080" }
$stalePids = @()
try {
  $conns = Get-NetTCPConnection -LocalPort ([int]$port) -State Listen -ErrorAction SilentlyContinue
  if ($conns) {
    $stalePids += $conns | Select-Object -ExpandProperty OwningProcess -Unique
  }
} catch {
  Write-Host "  (warn) could not inspect port $port : $($_.Exception.Message)" -ForegroundColor Yellow
}
$byName = Get-Process -Name "oral_training_backend" -ErrorAction SilentlyContinue
if ($byName) {
  $stalePids += $byName | Select-Object -ExpandProperty Id
}
$stalePids = $stalePids | Select-Object -Unique
foreach ($id in $stalePids) {
  $proc = Get-Process -Id $id -ErrorAction SilentlyContinue
  if ($proc) {
    Write-Host "  Stopping stale process PID $id ..." -ForegroundColor Yellow
    try { Stop-Process -Id $id -Force -ErrorAction Stop } catch {
      Write-Host "  (warn) failed to stop PID $id : $($_.Exception.Message)" -ForegroundColor Yellow
    }
  }
}
if ($stalePids) { Start-Sleep -Seconds 1 }

# 5. Refresh + verify executable
Write-Host "[3/4] Checking backend executable ..." -ForegroundColor Cyan
$exePath = Join-Path $root "oral_training_backend.exe"
$buildExe = Join-Path $root "build-msvc\Release\oral_training_backend.exe"
if (Test-Path $buildExe) {
  # Refresh whenever the build output is newer than the deployed copy. Copying
  # only when the exe is missing silently keeps launching a stale binary after
  # every rebuild, which looks exactly like "the route does not exist".
  $needCopy = $true
  if (Test-Path $exePath) {
    $needCopy = (Get-Item $buildExe).LastWriteTimeUtc -gt (Get-Item $exePath).LastWriteTimeUtc
  }
  if ($needCopy) {
    Write-Host "  Copying executable from build directory..." -ForegroundColor Yellow
    Copy-Item $buildExe $root -Force
  } else {
    Write-Host "  Deployed executable is up to date." -ForegroundColor Green
  }
} elseif (-not (Test-Path $exePath)) {
  Write-Host "  ERROR: oral_training_backend.exe not found. Build the project first!" -ForegroundColor Red
  Write-Host "  Run: cmake --build $root\build-msvc --config Release" -ForegroundColor Yellow
  exit 1
}

$libpq = Join-Path $root "libpq.dll"
if (-not (Test-Path $libpq)) {
  Write-Host "  ERROR: libpq.dll missing. Ensure all DLLs are in the backend directory." -ForegroundColor Red
  exit 1
}
Write-Host "  Backend executable ready." -ForegroundColor Green

# 6. Start backend
Write-Host "[4/4] Starting backend server ..." -ForegroundColor Cyan
$env:PATH = "$root;$env:PATH"
Write-Host "  http://$($env:BIND_ADDRESS):$($env:PORT)/api" -ForegroundColor White

& $exePath
exit $LASTEXITCODE
