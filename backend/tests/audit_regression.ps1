param(
  [Parameter(Mandatory = $true)][string]$DatabaseUrl,
  [string]$PsqlPath = 'C:\Program Files\PostgreSQL\18\bin\psql.exe'
)
$ErrorActionPreference = 'Stop'
if (([Uri]$DatabaseUrl).AbsolutePath.Trim('/') -notmatch '(?i)(test|_ci$)') {
  throw 'Use a disposable test database.'
}
$schema = 'audit_fix_' + [guid]::NewGuid().ToString('N').Substring(0, 10)
$backend = Split-Path $PSScriptRoot -Parent
$previousOptions = $env:PGOPTIONS
$previousUrl = $env:ORAL_TRAINING_TEST_DATABASE_URL
try {
  & $PsqlPath --dbname=$DatabaseUrl -X -v ON_ERROR_STOP=1 -c "CREATE SCHEMA $schema"
  if ($LASTEXITCODE -ne 0) { throw 'Failed to create isolated test schema.' }
  & (Join-Path $backend 'migrate.ps1') -DatabaseUrl $DatabaseUrl -PsqlPath $PsqlPath -Schema $schema
  # Re-running the official entry must be a no-op, including after 020 introduced v2 reports.
  & (Join-Path $backend 'migrate.ps1') -DatabaseUrl $DatabaseUrl -PsqlPath $PsqlPath -Schema $schema
  $env:PGOPTIONS = "-c search_path=$schema"
  $env:ORAL_TRAINING_TEST_DATABASE_URL = $DatabaseUrl
  & (Join-Path $backend 'build-msvc/Release/audit_regression_test.exe')
  if ($LASTEXITCODE -ne 0) { throw 'Audit regression assertions failed.' }
  $snapshotSql = "SELECT md5(COALESCE(jsonb_agg(to_jsonb(s) ORDER BY id)::text,'')) FROM sessions s;"
  $before = & $PsqlPath --dbname=$DatabaseUrl -X -At -v ON_ERROR_STOP=1 -c $snapshotSql
  if ($LASTEXITCODE -ne 0) { throw 'Failed to snapshot populated test sessions.' }
  & (Join-Path $backend 'migrate.ps1') -DatabaseUrl $DatabaseUrl -PsqlPath $PsqlPath -Schema $schema
  $after = & $PsqlPath --dbname=$DatabaseUrl -X -At -v ON_ERROR_STOP=1 -c $snapshotSql
  if ($LASTEXITCODE -ne 0 -or $before -ne $after) { throw 'Migration rerun changed populated session history.' }
} finally {
  $env:PGOPTIONS = $previousOptions
  $env:ORAL_TRAINING_TEST_DATABASE_URL = $previousUrl
  & $PsqlPath --dbname=$DatabaseUrl -X -v ON_ERROR_STOP=1 -c "DROP SCHEMA IF EXISTS $schema CASCADE"
}
