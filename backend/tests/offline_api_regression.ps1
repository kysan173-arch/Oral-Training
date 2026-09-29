param(
  [Parameter(Mandatory = $true)][string]$DatabaseUrl,
  [string]$PsqlPath = 'C:\Program Files\PostgreSQL\18\bin\psql.exe'
)
$ErrorActionPreference = 'Stop'
if (([Uri]$DatabaseUrl).AbsolutePath.Trim('/') -notmatch '(?i)(test|_ci$)') { throw 'Use a disposable test database.' }
$schema = 'offline_api_' + [guid]::NewGuid().ToString('N').Substring(0, 10)
$backendRoot = Split-Path $PSScriptRoot -Parent
$port = Get-Random -Minimum 28001 -Maximum 31000
$environmentNames = @('PGOPTIONS','DATABASE_URL','PRODUCTION','AUTH_MODE','ALLOW_RUNTIME_API_KEY',
  'BIND_ADDRESS','PORT','ALLOWED_ORIGIN','REQUIRE_HTTPS','DEEPSEEK_API_KEY','AI_WORKER_CONCURRENCY',
  'DATABASE_POOL_SIZE','RAG_ROLEPLAY_ENABLED','RAG_PATIENT_ENABLED','RAG_EVALUATION_V2_ENABLED')
$saved = @{}
foreach ($name in $environmentNames) { $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
$backendProcess = $null
try {
  & $PsqlPath --dbname=$DatabaseUrl -X -v ON_ERROR_STOP=1 -c "CREATE SCHEMA $schema"
  if ($LASTEXITCODE -ne 0) { throw 'Test schema creation failed.' }
  & (Join-Path $backendRoot 'migrate.ps1') -DatabaseUrl $DatabaseUrl -PsqlPath $PsqlPath -Schema $schema
  $env:PGOPTIONS = "-c search_path=$schema"
  $env:DATABASE_URL = $DatabaseUrl
  $env:PRODUCTION = 'false'
  $env:AUTH_MODE = 'demo'
  $env:ALLOW_RUNTIME_API_KEY = 'false'
  $env:BIND_ADDRESS = '127.0.0.1'
  $env:PORT = [string]$port
  $env:ALLOWED_ORIGIN = '*'
  $env:REQUIRE_HTTPS = 'false'
  $env:AI_WORKER_CONCURRENCY = '1'
  $env:DATABASE_POOL_SIZE = '8'
  $env:RAG_ROLEPLAY_ENABLED = 'false'
  $env:RAG_PATIENT_ENABLED = 'false'
  $env:RAG_EVALUATION_V2_ENABLED = 'false'
  Remove-Item Env:DEEPSEEK_API_KEY -ErrorAction SilentlyContinue
  $stdout = Join-Path $env:TEMP "$schema.stdout.log"
  $stderr = Join-Path $env:TEMP "$schema.stderr.log"
  $backendProcess = Start-Process -FilePath (Join-Path $backendRoot 'build-msvc/Release/oral_training_backend.exe') `
    -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
  $baseUrl = "http://127.0.0.1:$port/api"
  $reachable = $false
  foreach ($attempt in 1..60) {
    if ($backendProcess.HasExited) { throw 'Test backend exited during startup.' }
    try {
      $health = Invoke-WebRequest "$baseUrl/health" -SkipHttpErrorCheck -TimeoutSec 2
      if ($health.StatusCode -in @(200,503)) { $reachable = $true; break }
    } catch {}
    Start-Sleep -Milliseconds 250
  }
  if (-not $reachable) { throw 'Test backend did not become reachable.' }
  & (Join-Path $PSScriptRoot 'smoke.ps1') -BaseUrl $baseUrl
  if ($LASTEXITCODE -ne 0) { throw 'Normal smoke failed.' }
  & (Join-Path $PSScriptRoot 'state_machine.ps1') -DatabaseUrl $DatabaseUrl -BaseUrl $baseUrl -PsqlPath $PsqlPath
  & (Join-Path $PSScriptRoot 'session_concurrency.ps1') -DatabaseUrl $DatabaseUrl -BaseUrl $baseUrl -PsqlPath $PsqlPath
  Write-Output 'Offline API regression passed: smoke, state machine, concurrent creation'
} finally {
  if ($backendProcess -and -not $backendProcess.HasExited) {
    Stop-Process -Id $backendProcess.Id
    $backendProcess.WaitForExit()
  }
  foreach ($name in $environmentNames) { [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process') }
  & $PsqlPath --dbname=$DatabaseUrl -X -v ON_ERROR_STOP=1 -c "DROP SCHEMA IF EXISTS $schema CASCADE"
}
