param(
  [Parameter(Mandatory = $true)] [string]$DatabaseUrl,
  [string]$PsqlPath = 'C:\Program Files\PostgreSQL\18\bin\psql.exe',
  [string]$BackendPath = '',
  [switch]$KeepSchema
)

$ErrorActionPreference = 'Stop'
$databaseName = ([Uri]$DatabaseUrl).AbsolutePath.Trim('/')
if ($databaseName -notmatch '(?i)(test|ci)') {
  throw "Refusing to alter database '$databaseName'. Use a disposable database whose name contains test or ci."
}
if (-not (Test-Path -LiteralPath $PsqlPath)) { throw "psql not found: $PsqlPath" }

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $BackendPath) {
  $BackendPath = Join-Path $repositoryRoot 'backend\build-msvc\Release\oral_training_backend.exe'
}
if (-not (Test-Path -LiteralPath $BackendPath)) { throw "backend not found: $BackendPath" }

$schema = 'knowledge_api_' + [Guid]::NewGuid().ToString('N').Substring(0, 10)
$migrations = Join-Path $repositoryRoot 'backend\migrations'
$port = Get-Random -Minimum 20000 -Maximum 28000
$baseUrl = "http://127.0.0.1:$port/api"
$adminToken = 'knowledge-admin-api-token-00000000000000000001'
$learnerToken = 'knowledge-learner-api-token-0000000000000001'

function Get-Sha256Hex([string]$Value) {
  $sha = [System.Security.Cryptography.SHA256]::Create()
  try {
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($Value)
    return ([System.BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
  } finally {
    $sha.Dispose()
  }
}

function Invoke-Api {
  param(
    [string]$Method,
    [string]$Path,
    [string]$Token,
    [object]$Body,
    [int]$ExpectedStatus,
    [string]$IdempotencyKey = ''
  )
  $headers = @{ Authorization = "Bearer $Token" }
  if ($IdempotencyKey) { $headers['Idempotency-Key'] = $IdempotencyKey }
  $parameters = @{
    Uri = "$baseUrl$Path"
    Method = $Method
    Headers = $headers
    SkipHttpErrorCheck = $true
    TimeoutSec = 10
  }
  if ($null -ne $Body) {
    $parameters.ContentType = 'application/json; charset=utf-8'
    $parameters.Body = $Body | ConvertTo-Json -Depth 20 -Compress
  }
  $response = Invoke-WebRequest @parameters
  if ($response.StatusCode -ne $ExpectedStatus) {
    throw "$Method $Path returned $($response.StatusCode), expected $ExpectedStatus. Body: $($response.Content)"
  }
  return $response.Content | ConvertFrom-Json
}

$environmentNames = @(
  'PGOPTIONS', 'DATABASE_URL', 'PRODUCTION', 'AUTH_MODE', 'ALLOW_RUNTIME_API_KEY',
  'BIND_ADDRESS', 'PORT', 'ALLOWED_ORIGIN', 'REQUIRE_HTTPS', 'AI_WORKER_CONCURRENCY',
  'KNOWLEDGE_WORKER_CONCURRENCY', 'DATABASE_POOL_SIZE', 'DATABASE_POOL_WAIT_MS',
  'DEEPSEEK_API_KEY', 'RAG_ROLEPLAY_ENABLED', 'RAG_PATIENT_ENABLED', 'RAG_EVALUATION_V2_ENABLED'
)
$previousEnvironment = @{}
foreach ($name in $environmentNames) {
  $previousEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
$backend = $null
$stdout = Join-Path $env:TEMP "knowledge-api-$schema.stdout.log"
$stderr = Join-Path $env:TEMP "knowledge-api-$schema.stderr.log"

try {
  $env:RAG_ROLEPLAY_ENABLED = 'true'
  $env:RAG_PATIENT_ENABLED = 'true'
  $env:RAG_EVALUATION_V2_ENABLED = 'true'
  & $PsqlPath --dbname=$DatabaseUrl -v ON_ERROR_STOP=1 -X -q -c "CREATE SCHEMA $schema;"
  if ($LASTEXITCODE -ne 0) { throw 'Failed to create disposable schema.' }
  $env:PGOPTIONS = "-c search_path=$schema"
  # This smoke launches the current backend, so use every numbered migration.
  # Historical migration fixtures remain pinned in their dedicated tests.
  $migrationFiles = Get-ChildItem -LiteralPath $migrations -File -Filter '*.sql' |
    Where-Object { $_.Name -match '^\d{3}_.+\.sql$' } | Sort-Object Name
  foreach ($migration in $migrationFiles) {
    & $PsqlPath --dbname=$DatabaseUrl -v ON_ERROR_STOP=1 -X -q -f $migration.FullName
    if ($LASTEXITCODE -ne 0) { throw "Migration failed: $($migration.Name)" }
  }

  $adminHash = Get-Sha256Hex $adminToken
  $learnerHash = Get-Sha256Hex $learnerToken
  $seedSql = @"
INSERT INTO users(id, display_name, role, status, is_demo)
VALUES ('knowledge-api-admin', 'Knowledge API Admin', 'admin', 'active', TRUE),
       ('knowledge-api-learner', 'Knowledge API Learner', 'learner', 'active', TRUE);
INSERT INTO auth_sessions(token_hash, user_id, expires_at)
VALUES ('$adminHash', 'knowledge-api-admin', NOW() + INTERVAL '1 hour'),
       ('$learnerHash', 'knowledge-api-learner', NOW() + INTERVAL '1 hour');
"@
  & $PsqlPath --dbname=$DatabaseUrl -v ON_ERROR_STOP=1 -X -q -c $seedSql
  if ($LASTEXITCODE -ne 0) { throw 'Failed to seed API identities.' }

  $env:DATABASE_URL = $DatabaseUrl
  $env:PRODUCTION = 'false'
  $env:AUTH_MODE = 'demo'
  $env:ALLOW_RUNTIME_API_KEY = 'false'
  $env:BIND_ADDRESS = '127.0.0.1'
  $env:PORT = [string]$port
  $env:ALLOWED_ORIGIN = '*'
  $env:REQUIRE_HTTPS = 'false'
  $env:AI_WORKER_CONCURRENCY = '1'
  $env:KNOWLEDGE_WORKER_CONCURRENCY = '1'
  $env:DATABASE_POOL_SIZE = '8'
  $env:DATABASE_POOL_WAIT_MS = '3000'
  Remove-Item Env:DEEPSEEK_API_KEY -ErrorAction SilentlyContinue

  $backend = Start-Process -FilePath $BackendPath -PassThru -WindowStyle Hidden `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr
  $reachable = $false
  foreach ($attempt in 1..60) {
    Start-Sleep -Milliseconds 250
    if ($backend.HasExited) { break }
    try {
      $health = Invoke-WebRequest -Uri "$baseUrl/health" -SkipHttpErrorCheck -TimeoutSec 2
      if ($health.StatusCode -in @(200, 503)) { $reachable = $true; break }
    } catch {}
  }
  if (-not $reachable) { throw 'Backend did not become reachable for knowledge API test.' }

  $private = Invoke-Api GET '/supervisor/members/knowledge-api-learner/sessions/private-session' $adminToken $null 403
  if ($private.code -ne 'LEARNER_CONTENT_PRIVATE') { throw 'Admin conversation access must be denied.' }

  $unknownDuration = @{ status = 'unknown'; reason = 'Not recorded in test fixture' }
  $servicePayload = @{
    name = 'API Test Service'
    category = 'implant'
    dataOrigin = 'synthetic'
    price = @{ status = 'unknown'; reason = 'Not recorded in test fixture' }
    includedItems = @('Test consultation')
    excludedItems = @()
    visitDuration = $unknownDuration
    treatmentDuration = $unknownDuration
    followupInterval = $unknownDuration
    appointment = @{ status = 'unknown'; reason = 'No live availability in fixture' }
    professionalTopics = @('implant-components')
    scenarioIds = @('implant-basic')
  }

  $forbidden = Invoke-Api POST '/admin/services' $learnerToken @{ payload = $servicePayload } 403
  if ($forbidden.code -ne 'ROLE_FORBIDDEN') { throw 'Learner admin write did not return ROLE_FORBIDDEN.' }
  $created = Invoke-Api POST '/admin/services' $adminToken @{ payload = $servicePayload } 201
  $serviceId = $created.data.id
  $draft = Invoke-Api GET "/admin/services/$serviceId/draft" $adminToken $null 200
  if ($draft.data.draftVersion -ne 1) { throw 'New service draft version was not 1.' }
  $servicePayload.name = 'API Test Service Updated'
  $saved = Invoke-Api PUT "/admin/services/$serviceId/draft" $adminToken `
    @{ draftVersion = 1; payload = $servicePayload } 200
  if ($saved.data.draftVersion -ne 2) { throw 'Service draft save did not advance version.' }
  $conflict = Invoke-Api PUT "/admin/services/$serviceId/draft" $adminToken `
    @{ draftVersion = 1; payload = $servicePayload } 409
  if ($conflict.code -ne 'DRAFT_VERSION_CONFLICT') { throw 'Stale service save did not conflict.' }
  $published = Invoke-Api POST "/admin/services/$serviceId/publish" $adminToken `
    @{ draftVersion = 2 } 200 'api-service-publish-1'
  $replayed = Invoke-Api POST "/admin/services/$serviceId/publish" $adminToken `
    @{ draftVersion = 2 } 200 'api-service-publish-1'
  if ($published.data.revision.revisionId -ne $replayed.data.revision.revisionId -or
      -not $replayed.data.replayed) { throw 'Service publish was not idempotent.' }

  $metadata = @{
    origin = 'synthetic'; verification = 'unverified'; sourceTitle = ''
    sourceUrl = $null; sourceLocator = ''; applicability = 'API test only'
    trainingScope = 'demo'; aliases = @('implant')
  }
  $knowledge = Invoke-Api POST '/admin/knowledge' $adminToken @{
    topic = 'implant-components'; scope = 'service'; serviceId = $serviceId
    title = 'API test knowledge'; body = 'Fixed synthetic content for API testing only.'
    metadata = $metadata
  } 201
  $entryId = $knowledge.data.id
  Invoke-Api POST "/admin/knowledge/$entryId/publish" $adminToken `
    @{ draftVersion = 1 } 200 'api-knowledge-publish-1' | Out-Null
  $preview = Invoke-Api POST '/admin/knowledge/preview' $adminToken `
    @{ entityType = 'knowledge'; entityId = $entryId; draftVersion = 1; question = 'implant content' } 200
  if ($preview.data.retrievalStatus -ne 'ok' -or $preview.data.evidence.Count -lt 1) {
    throw 'Knowledge preview did not return deterministic evidence.'
  }
  $chunkCount = & $PsqlPath --dbname=$DatabaseUrl -v ON_ERROR_STOP=1 -X -q -t `
    -c "SELECT COUNT(*) FROM knowledge_chunks WHERE revision_id=(SELECT current_revision_id FROM knowledge_entries WHERE id='$entryId');"
  $chunkCountValue = [int](($chunkCount | ForEach-Object { $_.Trim() } |
    Where-Object { $_ -match '^\d+$' } | Select-Object -Last 1))
  if ($chunkCountValue -lt 1) { throw 'Knowledge publish did not create retrieval chunks.' }

  $services = Invoke-Api GET '/services' $learnerToken $null 200
  if (-not ($services.data.items | Where-Object { $_.id -eq $serviceId })) {
    throw 'Published service was not selectable by learner.'
  }
  $roleplay = Invoke-Api POST '/roleplay/sessions' $learnerToken @{
    scenarioId = 'implant-basic'; serviceId = $serviceId; clientSessionId = 'api-roleplay-rag-1'
  } 201
  if ($roleplay.data.session.contextVersion -ne 2 -or
      $roleplay.data.session.serviceId -ne $serviceId) {
    throw 'Roleplay session did not lock the selected service context.'
  }
  $roleplayReplay = Invoke-Api POST '/roleplay/sessions' $learnerToken @{
    scenarioId = 'implant-basic'; serviceId = $serviceId; clientSessionId = 'api-roleplay-rag-1'
  } 201
  if ($roleplayReplay.data.session.id -ne $roleplay.data.session.id) {
    throw 'Roleplay clientSessionId replay created another session.'
  }

  # N02 transport contract: the default gateway without a key fails explicitly without a model call.
  $trainingRequest = @{ scenarioId = 'implant-basic'; serviceId = $serviceId; clientSessionId = 'api-training-init-1' }
  $training = Invoke-Api POST '/sessions' $learnerToken $trainingRequest 202
  $trainingId = $training.data.session.id
  if ($training.data.session.contextVersion -ne 2 -or $training.data.session.serviceId -ne $serviceId) {
    throw 'Training creation did not expose the locked service.'
  }
  $trainingReplay = Invoke-Api POST '/sessions' $learnerToken $trainingRequest 202
  if ($trainingReplay.data.session.id -ne $trainingId) { throw 'Training replay created another session.' }
  $different = Invoke-Api POST '/sessions' $learnerToken @{
    scenarioId = 'price-comparison'; serviceId = $serviceId; clientSessionId = 'api-training-init-1'
  } 409
  if ($different.code -ne 'IDEMPOTENCY_CONFLICT') { throw 'Training replay did not compare parameters.' }
  foreach ($attempt in 1..40) {
    $initialization = Invoke-Api GET "/sessions/$trainingId/initialization" $learnerToken $null 200
    if ($initialization.data.status -eq 'failed') { break }
    Start-Sleep -Milliseconds 100
  }
  if ($initialization.data.status -ne 'failed' -or
      $initialization.data.errorType -ne 'MODEL_NOT_CONFIGURED' -or
      $null -ne $initialization.data.publicProfile) {
    throw 'Unavailable initializer did not fail safely.'
  }
  foreach ($action in @('messages', 'hint', 'finish')) {
    $blocked = Invoke-Api POST "/sessions/$trainingId/$action" $learnerToken @{
      clientMessageId = 'blocked-init-message'; content = 'hello'
    } 409
    if ($blocked.code -ne 'PATIENT_INITIALIZATION_FAILED') { throw "Uninitialized $action was not blocked." }
  }
  $retry = Invoke-Api POST "/sessions/$trainingId/initialization/retry" $learnerToken @{} 202
  if ($retry.data.generation -ne 2) { throw 'Initialization retry did not advance generation.' }
  Invoke-Api POST "/sessions/$trainingId/abandon" $learnerToken @{} 202 | Out-Null

  $job = Invoke-Api POST '/admin/knowledge/generation-jobs' $adminToken @{
    kind = 'knowledge_draft'; draftId = $knowledge.data.draftId
    brief = 'Generate a synthetic candidate'; count = 1
  } 202 'api-generation-1'
  if ($job.data.promptVersion -ne 'knowledge-draft-v1') {
    throw 'Knowledge generation job did not expose its versioned prompt.'
  }
  Invoke-Api GET "/admin/knowledge/generation-jobs/$($job.data.jobId)" $adminToken $null 200 | Out-Null
  Invoke-Api POST "/admin/knowledge/$entryId/archive" $adminToken @{} 200 | Out-Null
  Invoke-Api POST "/admin/services/$serviceId/archive" $adminToken @{} 200 | Out-Null
  [pscustomobject]@{ Result = 'passed'; Schema = $schema; Port = $port } | ConvertTo-Json -Compress
} finally {
  if ($backend -and -not $backend.HasExited) { Stop-Process -Id $backend.Id -Force }
  if (Test-Path -LiteralPath $stderr) {
    $errors = Get-Content -LiteralPath $stderr -ErrorAction SilentlyContinue
    if ($errors) { $errors | Select-Object -Last 20 }
  }
  foreach ($name in $environmentNames) {
    [Environment]::SetEnvironmentVariable($name, $previousEnvironment[$name], 'Process')
  }
  if (-not $KeepSchema) {
    & $PsqlPath --dbname=$DatabaseUrl -v ON_ERROR_STOP=1 -X -q `
      -c "DROP SCHEMA IF EXISTS $schema CASCADE;"
  }
  Remove-Item -LiteralPath $stdout, $stderr -Force -ErrorAction SilentlyContinue
}
