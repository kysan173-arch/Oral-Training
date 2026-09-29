param(
  [Parameter(Mandatory = $true)] [string]$DatabaseUrl,
  [string]$PsqlPath = 'C:\Program Files\PostgreSQL\18\bin\psql.exe',
  [string]$ExecutablePath = '',
  [switch]$KeepSchema
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $PsqlPath)) { throw "psql not found: $PsqlPath" }

$databaseName = ([Uri]$DatabaseUrl).AbsolutePath.Trim('/')
if ($databaseName -notmatch '(?i)(test|ci)') {
  throw "Refusing to alter database '$databaseName'. Use a disposable database whose name contains test or ci."
}

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $ExecutablePath) {
  $ExecutablePath = Join-Path $repositoryRoot 'backend\build-msvc\Release\patient_initialization_test.exe'
}
if (-not (Test-Path -LiteralPath $ExecutablePath)) {
  throw "patient initialization test executable not found: $ExecutablePath"
}

$schema = 'patient_init_' + [Guid]::NewGuid().ToString('N').Substring(0, 10)
$migrations = Join-Path $repositoryRoot 'backend\migrations'
$previousOptions = $env:PGOPTIONS
$previousTestUrl = $env:ORAL_TRAINING_TEST_DATABASE_URL

try {
  & $PsqlPath --dbname=$DatabaseUrl -v ON_ERROR_STOP=1 -X -q -c "CREATE SCHEMA $schema;"
  if ($LASTEXITCODE -ne 0) { throw 'Failed to create disposable schema.' }
  $env:PGOPTIONS = "-c search_path=$schema"
  $migrationFiles = Get-ChildItem -LiteralPath $migrations -File -Filter '*.sql' |
    Where-Object { $_.Name -match '^\d{3}_.+\.sql$' } | Sort-Object Name
  foreach ($migration in $migrationFiles) {
    if ($migration.Name -eq '032_patient_initialization_jobs.sql') {
      # Representative pre-N02 history: completed and active legacy sessions plus a pending job.
      $history = @"
INSERT INTO sessions(id,user_id,scenario_id,scenario_name,status,current_round,max_rounds,patient_state)
VALUES ('init-migration-active','demo-user-001','implant-basic','Legacy active','in_progress',0,10,'{}'),
       ('init-migration-done','demo-user-001','price-comparison','Legacy done','completed',1,10,'{}');
INSERT INTO evaluations(session_id,status,report) VALUES ('init-migration-done','generating',NULL);
INSERT INTO ai_jobs(id,job_type,target_id,dedupe_key,status,available_at)
VALUES ('init-migration-job','evaluation','init-migration-done','evaluation:init-migration-done','pending',NOW()+INTERVAL '1 day');
"@
      & $PsqlPath --dbname=$DatabaseUrl -v ON_ERROR_STOP=1 -X -q -c $history
      if ($LASTEXITCODE -ne 0) { throw 'N02 historical fixture failed.' }
    }
    & $PsqlPath --dbname=$DatabaseUrl -v ON_ERROR_STOP=1 -X -q -f $migration.FullName
    if ($LASTEXITCODE -ne 0) { throw "Migration failed: $($migration.Name)" }
  }
  # Verify the additive migration can be rerun without rewriting existing contexts.
  & $PsqlPath --dbname=$DatabaseUrl -v ON_ERROR_STOP=1 -X -q -f (Join-Path $migrations '032_patient_initialization_jobs.sql')
  if ($LASTEXITCODE -ne 0) { throw 'N02 migration rerun failed.' }


  $env:ORAL_TRAINING_TEST_DATABASE_URL = $DatabaseUrl
  & $ExecutablePath
  if ($LASTEXITCODE -ne 0) { throw 'Patient initialization test failed.' }
  $digestSql = "SELECT md5(string_agg(row_to_json(c)::text, '' ORDER BY id)) FROM training_contexts c;"
  $before = & $PsqlPath --dbname=$DatabaseUrl -v ON_ERROR_STOP=1 -X -Atc $digestSql
  if ($LASTEXITCODE -ne 0) { throw 'Snapshot digest failed.' }
  & $PsqlPath --dbname=$DatabaseUrl -v ON_ERROR_STOP=1 -X -q -f (Join-Path $migrations '032_patient_initialization_jobs.sql')
  if ($LASTEXITCODE -ne 0) { throw 'Populated N02 migration rerun failed.' }
  $after = & $PsqlPath --dbname=$DatabaseUrl -v ON_ERROR_STOP=1 -X -Atc $digestSql
  if ($LASTEXITCODE -ne 0 -or "$before" -ne "$after") { throw 'Migration rerun changed a stored context.' }

} finally {
  $env:PGOPTIONS = $previousOptions
  $env:ORAL_TRAINING_TEST_DATABASE_URL = $previousTestUrl
  if (-not $KeepSchema) {
    & $PsqlPath --dbname=$DatabaseUrl -v ON_ERROR_STOP=1 -X -q -c "DROP SCHEMA IF EXISTS $schema CASCADE;"
  }
}
