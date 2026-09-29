param(
  [Parameter(Mandatory = $true)][string]$DatabaseUrl,
  [string]$PsqlPath = 'psql',
  [ValidatePattern('^[a-zA-Z_][a-zA-Z0-9_]*$')][string]$Schema = 'public',
  # Existing installations must explicitly identify the last applied migration.
  [ValidateRange(0, 999)][int]$BaselineThrough = 0
)
$ErrorActionPreference = 'Stop'
$migrationFiles = @(Get-ChildItem (Join-Path $PSScriptRoot 'migrations') -Filter '*.sql' |
  Where-Object { $_.Name -match '^\d{3}_' } | Sort-Object Name)
if (-not $migrationFiles.Count) { throw 'No numbered migrations found.' }
$sql = [System.Collections.Generic.List[string]]::new()
$sql.Add('\set ON_ERROR_STOP on')
$sql.Add("SET search_path TO $Schema;")
$sql.Add("SELECT pg_advisory_lock(hashtext(current_database()), hashtext('${Schema}:migrations'));")
$sql.Add("SELECT to_regclass('schema_migrations') IS NULL AS no_ledger, to_regclass('sessions') IS NOT NULL AS existing_install \gset")
$sql.Add('\if :no_ledger')
$sql.Add('\if :existing_install')
if ($BaselineThrough -eq 0) {
  $sql.Add("DO `$`$ BEGIN RAISE EXCEPTION 'Existing database requires -BaselineThrough with the verified last applied migration; stop the backend first'; END `$`$;")
}
$sql.Add('\endif')
$sql.Add('\endif')
$sql.Add('CREATE TABLE IF NOT EXISTS schema_migrations (name TEXT PRIMARY KEY, sha256 TEXT NOT NULL, applied_at TIMESTAMPTZ NOT NULL DEFAULT NOW(), baselined BOOLEAN NOT NULL DEFAULT FALSE);')
foreach ($migration in $migrationFiles) {
  $name = $migration.Name
  $number = [int]$name.Substring(0, 3)
  $digest = (Get-FileHash -LiteralPath $migration.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
  if ($number -le $BaselineThrough) {
    $sql.Add("INSERT INTO schema_migrations(name,sha256,baselined) VALUES ('$name','$digest',TRUE) ON CONFLICT DO NOTHING;")
  }
  $sql.Add("DO `$`$ BEGIN IF EXISTS (SELECT 1 FROM schema_migrations WHERE name='$name' AND sha256<>'$digest') THEN RAISE EXCEPTION 'Shipped migration checksum changed: $name'; END IF; END `$`$;")
  $sql.Add("SELECT NOT EXISTS (SELECT 1 FROM schema_migrations WHERE name='$name') AS apply_migration \gset")
  $sql.Add('\if :apply_migration')
  if ($number -eq 5) {
    $guardPath = (Join-Path $PSScriptRoot 'migration-support/005_archive_guard.sql').Replace('\', '/').Replace("'", "''")
    $sql.Add("\i '$guardPath'")
  }
  $migrationPath = $migration.FullName.Replace('\', '/').Replace("'", "''")
  $sql.Add("\echo Applying $name")
  $sql.Add("\i '$migrationPath'")
  $sql.Add("INSERT INTO schema_migrations(name,sha256) VALUES ('$name','$digest');")
  $sql.Add('\endif')
}
$scriptPath = Join-Path ([IO.Path]::GetTempPath()) ('oral-migrate-' + [guid]::NewGuid().ToString('N') + '.sql')
try {
  [IO.File]::WriteAllLines($scriptPath, $sql, [Text.UTF8Encoding]::new($false))
  & $PsqlPath --dbname=$DatabaseUrl -X -v ON_ERROR_STOP=1 -f $scriptPath
  if ($LASTEXITCODE -ne 0) { throw 'Migration failed; see the PostgreSQL error above.' }
} finally {
  Remove-Item -LiteralPath $scriptPath -ErrorAction SilentlyContinue
}
