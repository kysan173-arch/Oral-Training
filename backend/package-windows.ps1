param(
  [string]$Configuration = 'Release',
  [string]$PostgreSqlBin = 'C:\Program Files\PostgreSQL\18\bin',
  [string]$VcRuntimeDir = '',
  [string]$PackageName = 'oral-training-backend-mvp-windows-x64'
)

$ErrorActionPreference = 'Stop'

$backendRoot = $PSScriptRoot
$projectRoot = Split-Path $backendRoot -Parent
$sourceExe = Join-Path $backendRoot "build-msvc\$Configuration\oral_training_backend.exe"
$distRoot = Join-Path $backendRoot 'dist'
$packageRoot = Join-Path $distRoot $PackageName
$zipPath = Join-Path $distRoot "$PackageName.zip"

if (-not (Test-Path -LiteralPath $sourceExe)) {
  throw "Backend executable not found: $sourceExe"
}
if (-not (Test-Path -LiteralPath $PostgreSqlBin)) {
  throw "PostgreSQL bin directory not found: $PostgreSqlBin"
}
if (Test-Path -LiteralPath $packageRoot) {
  throw "Package directory already exists. Remove it before rebuilding: $packageRoot"
}
if (Test-Path -LiteralPath $zipPath) {
  throw "Package archive already exists. Remove it before rebuilding: $zipPath"
}

if (-not $VcRuntimeDir) {
  $vcRoot = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC'
  $VcRuntimeDir = Get-ChildItem -LiteralPath $vcRoot -Directory |
    Where-Object { $_.Name -match '^\d' } |
    Sort-Object Name -Descending |
    ForEach-Object { Join-Path $_.FullName 'x64\Microsoft.VC143.CRT' } |
    Where-Object { Test-Path -LiteralPath $_ } |
    Select-Object -First 1
}
if (-not $VcRuntimeDir -or -not (Test-Path -LiteralPath $VcRuntimeDir)) {
  throw 'Visual C++ x64 runtime directory was not found. Pass -VcRuntimeDir explicitly.'
}

New-Item -ItemType Directory -Path $packageRoot | Out-Null
New-Item -ItemType Directory -Path (Join-Path $packageRoot 'migrations') | Out-Null

Copy-Item -LiteralPath $sourceExe -Destination (Join-Path $packageRoot 'oral_training_backend.exe')

$postgresDlls = @(
  'libpq.dll',
  'libssl-3-x64.dll',
  'libcrypto-3-x64.dll',
  'libintl-9.dll',
  'libiconv-2.dll',
  'libwinpthread-1.dll'
)
foreach ($dll in $postgresDlls) {
  Copy-Item -LiteralPath (Join-Path $PostgreSqlBin $dll) -Destination $packageRoot
}

$vcDlls = @('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')
foreach ($dll in $vcDlls) {
  Copy-Item -LiteralPath (Join-Path $VcRuntimeDir $dll) -Destination $packageRoot
}

Copy-Item -LiteralPath (Join-Path $backendRoot '.env.example') -Destination (Join-Path $packageRoot 'backend.env.example')
Copy-Item -Path (Join-Path $backendRoot 'migrations\*.sql') -Destination (Join-Path $packageRoot 'migrations')
Copy-Item -LiteralPath (Join-Path $backendRoot 'portable\README.txt') -Destination $packageRoot
Copy-Item -LiteralPath (Join-Path $backendRoot 'portable\start-backend.cmd') -Destination $packageRoot
Copy-Item -LiteralPath (Join-Path $backendRoot 'portable\start-backend.ps1') -Destination $packageRoot

Compress-Archive -LiteralPath $packageRoot -DestinationPath $zipPath -CompressionLevel Optimal
$hash = Get-FileHash -LiteralPath $zipPath -Algorithm SHA256

[pscustomobject]@{
  Package = $zipPath
  SizeBytes = (Get-Item -LiteralPath $zipPath).Length
  SHA256 = $hash.Hash
  ProjectRoot = $projectRoot
} | ConvertTo-Json -Compress
