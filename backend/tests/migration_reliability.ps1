param(
  [Parameter(Mandatory = $true)] [string]$DatabaseUrl,
  [string]$PsqlPath = 'C:\Program Files\PostgreSQL\18\bin\psql.exe',
  [switch]$KeepSchemas
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $PsqlPath)) { throw "psql not found: $PsqlPath" }

$databaseName = ([Uri]$DatabaseUrl).AbsolutePath.Trim('/')
if ($databaseName -notmatch '(?i)(test|ci)') {
  throw "Refusing to alter database '$databaseName'. Use a disposable database whose name contains test or ci."
}

$suffix = [Guid]::NewGuid().ToString('N').Substring(0, 10)
$emptySchema = "reliability_empty_$suffix"
$historySchema = "reliability_history_$suffix"
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$migrations = Join-Path $repositoryRoot 'backend\migrations'
$fixture = Join-Path $PSScriptRoot 'fixtures\reliability_history.sql'
$previousOptions = $env:PGOPTIONS

function Invoke-Psql {
  param([string]$Schema, [string]$File, [string]$Command)
  $env:PGOPTIONS = "-c search_path=$Schema"
  $arguments = @($DatabaseUrl, '-v', 'ON_ERROR_STOP=1', '-X', '-q')
  if ($File) { $arguments += @('-f', $File) }
  if ($Command) { $arguments += @('-c', $Command) }
  & $PsqlPath @arguments
  if ($LASTEXITCODE -ne 0) { throw "psql failed for schema $Schema" }
}

try {
  & $PsqlPath --dbname=$DatabaseUrl -v ON_ERROR_STOP=1 -X -q -c "CREATE SCHEMA $emptySchema; CREATE SCHEMA $historySchema;"
  if ($LASTEXITCODE -ne 0) { throw 'Failed to create disposable schemas.' }

  foreach ($schema in @($emptySchema, $historySchema)) {
    Invoke-Psql $schema (Join-Path $migrations '001_initial.sql') ''
    Invoke-Psql $schema (Join-Path $migrations '002_roleplay.sql') ''
  }

  Invoke-Psql $emptySchema (Join-Path $migrations '003_reliability.sql') ''
  Invoke-Psql $emptySchema (Join-Path $migrations '004_identity.sql') ''
  Invoke-Psql $emptySchema (Join-Path $migrations '005_pair_and_state_repair.sql') ''
  Invoke-Psql $emptySchema (Join-Path $migrations '006_learner_insights.sql') ''
  Invoke-Psql $emptySchema (Join-Path $migrations '007_training_experience.sql') ''
  Invoke-Psql $emptySchema (Join-Path $migrations '008_supervisor_growth.sql') ''
  Invoke-Psql $emptySchema (Join-Path $migrations '009_legacy_report_totals.sql') ''
  Invoke-Psql $emptySchema '' @'
DO $$ BEGIN
  IF to_regclass('message_repair_archive') IS NULL OR to_regclass('ai_jobs') IS NULL OR
     to_regclass('users') IS NULL OR to_regclass('auth_sessions') IS NULL OR
     to_regclass('message_pair_repair_audit') IS NULL OR
     to_regclass('generation_state_repair_archive') IS NULL OR
     to_regclass('learner_mistake_progress') IS NULL OR to_regclass('session_hints') IS NULL OR
     to_regclass('learner_checkins') IS NULL OR to_regclass('learner_phrase_favorites') IS NULL THEN
    RAISE EXCEPTION 'empty database migration did not create required tables';
  END IF;
END $$;
'@

  Invoke-Psql $historySchema $fixture ''
  Invoke-Psql $historySchema (Join-Path $migrations '003_reliability.sql') ''
  Invoke-Psql $historySchema (Join-Path $migrations '004_identity.sql') ''
  Invoke-Psql $historySchema (Join-Path $migrations '006_learner_insights.sql') ''
  Invoke-Psql $historySchema (Join-Path $migrations '007_training_experience.sql') ''
  Invoke-Psql $historySchema (Join-Path $migrations '008_supervisor_growth.sql') ''
  Invoke-Psql $historySchema '' @'
INSERT INTO learner_mistake_progress(user_id, session_id, mistake_key, mastered_at)
VALUES ('demo-user-001', 'test-max-rounds', 'fixture-mistake', NOW());
INSERT INTO session_hints(id, session_id, hint_number, content)
VALUES ('fixture-hint', 'test-max-rounds', 1, 'Confirm the concern before explaining the clinical assessment boundary.');
INSERT INTO learner_checkins(user_id, checkin_date, points)
VALUES ('demo-user-001', DATE '2026-01-02', 10);
INSERT INTO learner_phrase_favorites(user_id, session_id, phrase_key)
VALUES ('demo-user-001', 'test-max-rounds', 'fixture-phrase');
'@
  Invoke-Psql $historySchema '' @'
DO $$ BEGIN
  IF (SELECT COUNT(*) FROM message_repair_archive) <> 5 THEN
    RAISE EXCEPTION 'expected five archived duplicate rows';
  END IF;
  IF NOT EXISTS (SELECT 1 FROM messages WHERE id = 'input-earliest') OR
     NOT EXISTS (SELECT 1 FROM messages WHERE id = 'reply-latest') OR
     EXISTS (SELECT 1 FROM messages WHERE id IN ('input-later', 'reply-earlier')) THEN
    RAISE EXCEPTION 'training duplicate repair kept the wrong rows';
  END IF;
  IF NOT EXISTS (SELECT 1 FROM roleplay_messages WHERE id = 'rp-input-earliest') OR
     NOT EXISTS (SELECT 1 FROM roleplay_messages WHERE id = 'rp-reply-latest') OR
     EXISTS (SELECT 1 FROM roleplay_messages WHERE id IN ('rp-input-later', 'rp-reply-earlier')) THEN
    RAISE EXCEPTION 'roleplay duplicate repair kept the wrong rows';
  END IF;
  IF (SELECT reply_status FROM messages WHERE id = 'input-earliest') <> 'ready' OR
     (SELECT reply_status FROM roleplay_messages WHERE id = 'rp-input-earliest') <> 'ready' THEN
    RAISE EXCEPTION 'complete rounds were not backfilled ready';
  END IF;
  IF (SELECT COUNT(*) FROM ai_jobs WHERE status = 'pending') <> 2 THEN
    RAISE EXCEPTION 'generating records were not backfilled as jobs';
  END IF;
  IF (SELECT status FROM sessions WHERE id = 'test-max-rounds') <> 'in_progress' THEN
    RAISE EXCEPTION 'max-round historical session was changed destructively';
  END IF;
END $$;
'@

  Invoke-Psql $historySchema (Join-Path $migrations '005_pair_and_state_repair.sql') ''
  Invoke-Psql $historySchema '' @'
DO $$ BEGIN
  IF (SELECT COUNT(*) FROM message_repair_archive) <> 7 THEN
    RAISE EXCEPTION 'paired repair did not archive the two displaced live inputs';
  END IF;
  IF (SELECT COUNT(*) FROM message_pair_repair_audit WHERE repair_status = 'resolved') <> 2 OR
     (SELECT COUNT(*) FROM message_pair_repair_audit WHERE repair_status = 'unresolved') <> 1 THEN
    RAISE EXCEPTION 'paired repair audit is incomplete';
  END IF;
  IF NOT EXISTS (SELECT 1 FROM messages WHERE id = 'unresolved-input-earliest') OR
     EXISTS (SELECT 1 FROM messages WHERE id = 'unresolved-input-later') THEN
    RAISE EXCEPTION 'unresolved round was changed after audit';
  END IF;
  IF NOT EXISTS (SELECT 1 FROM messages WHERE id = 'input-later') OR
     NOT EXISTS (SELECT 1 FROM messages WHERE id = 'reply-latest') OR
     EXISTS (SELECT 1 FROM messages WHERE id IN ('input-earliest', 'reply-earlier')) THEN
    RAISE EXCEPTION 'training paired repair did not keep the newest complete attempt';
  END IF;
  IF NOT EXISTS (SELECT 1 FROM roleplay_messages WHERE id = 'rp-input-later') OR
     NOT EXISTS (SELECT 1 FROM roleplay_messages WHERE id = 'rp-reply-latest') OR
     EXISTS (SELECT 1 FROM roleplay_messages WHERE id IN ('rp-input-earliest', 'rp-reply-earlier')) THEN
    RAISE EXCEPTION 'roleplay paired repair did not keep the newest complete attempt';
  END IF;
  IF (SELECT reply_status FROM messages WHERE id = 'input-later') <> 'ready' OR
     (SELECT reply_status FROM roleplay_messages WHERE id = 'rp-input-later') <> 'ready' THEN
    RAISE EXCEPTION 'repaired inputs were not marked ready';
  END IF;
  IF (SELECT COUNT(*) FROM evaluations WHERE session_id IN
        ('test-duplicate', 'test-missing-evaluation') AND status = 'generating') <> 2 OR
     (SELECT COUNT(*) FROM roleplay_summaries WHERE session_id IN
        ('test-roleplay-duplicate', 'test-roleplay-missing-summary') AND status = 'generating') <> 2 THEN
    RAISE EXCEPTION 'missing generation state was not rebuilt';
  END IF;
  IF (SELECT COUNT(*) FROM ai_jobs WHERE status = 'pending') <> 4 OR
     (SELECT generation FROM ai_jobs WHERE dedupe_key = 'evaluation:test-duplicate') <> 2 OR
     (SELECT generation FROM ai_jobs WHERE dedupe_key =
        'roleplay-summary:test-roleplay-duplicate') <> 2 THEN
    RAISE EXCEPTION 'repaired histories did not open new task generations';
  END IF;
  IF (SELECT COUNT(*) FROM generation_state_repair_archive) <> 4 THEN
    RAISE EXCEPTION 'replaced report and job states were not archived';
  END IF;
END $$;
'@

  Invoke-Psql $historySchema (Join-Path $migrations '003_reliability.sql') ''
  Invoke-Psql $historySchema (Join-Path $migrations '004_identity.sql') ''
  Invoke-Psql $historySchema (Join-Path $migrations '005_pair_and_state_repair.sql') ''
  Invoke-Psql $historySchema '' @'
DO $$ BEGIN
  IF (SELECT COUNT(*) FROM message_repair_archive) <> 7 OR
     (SELECT COUNT(*) FROM message_pair_repair_audit) <> 3 OR
     (SELECT COUNT(*) FROM generation_state_repair_archive) <> 4 OR
     (SELECT COUNT(*) FROM ai_jobs) <> 4 THEN
    RAISE EXCEPTION 'rerunning migrations changed repaired history';
  END IF;
END $$;
'@
  Invoke-Psql $historySchema (Join-Path $migrations '006_learner_insights.sql') ''
  Invoke-Psql $historySchema (Join-Path $migrations '007_training_experience.sql') ''
  Invoke-Psql $historySchema (Join-Path $migrations '008_supervisor_growth.sql') ''
  Invoke-Psql $historySchema '' @'
DO $$ BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM learner_mistake_progress
    WHERE user_id = 'demo-user-001' AND session_id = 'test-max-rounds'
      AND mistake_key = 'fixture-mistake' AND mastered_at IS NOT NULL
  ) THEN
    RAISE EXCEPTION 'learner insight progress was not preserved on migration rerun';
  END IF;
  IF NOT EXISTS (
    SELECT 1 FROM session_hints
    WHERE id = 'fixture-hint' AND session_id = 'test-max-rounds' AND hint_number = 1
  ) THEN
    RAISE EXCEPTION 'training hint was not preserved on migration rerun';
  END IF;
  IF NOT EXISTS (
    SELECT 1 FROM learner_checkins
    WHERE user_id = 'demo-user-001' AND checkin_date = DATE '2026-01-02' AND points = 10
  ) THEN
    RAISE EXCEPTION 'daily check-in was not preserved on migration rerun';
  END IF;
  IF NOT EXISTS (
    SELECT 1 FROM learner_phrase_favorites
    WHERE user_id = 'demo-user-001' AND session_id = 'test-max-rounds' AND phrase_key = 'fixture-phrase'
  ) THEN
    RAISE EXCEPTION 'phrase favorite was not preserved on migration rerun';
  END IF;
END $$;
'@
  # Exercise the actual upgrade path: v005 archives a legacy report first.
  Invoke-Psql $historySchema '' @'
INSERT INTO sessions(id,user_id,scenario_id,scenario_name,status,current_round,max_rounds,
  patient_state,evaluation_status,total_score,finished_at)
SELECT id,'demo-user-001','implant-basic','Legacy report','completed',1,10,'{}'::jsonb,'ready',80,NOW()
FROM (VALUES ('legacy-restore'),('legacy-changed'),('legacy-new')) AS fixture(id);
INSERT INTO evaluations(session_id,status,report,model_version,prompt_version,generated_at)
SELECT id,'ready','{"summary":"original evidence","dimensionScores":{"knowledgeAccuracy":80,
  "medicalCompliance":80,"empathy":80,"needsDiscovery":80,"serviceEtiquette":80}}'::jsonb,
  'legacy-model','legacy-prompt',TIMESTAMPTZ '2026-01-01 00:00:00+08'
FROM sessions WHERE id IN ('legacy-restore','legacy-changed','legacy-new');
'@
  Invoke-Psql $historySchema (Join-Path $migrations '005_pair_and_state_repair.sql') ''
  Invoke-Psql $historySchema '' @'
INSERT INTO message_pair_repair_audit(source_table,session_id,round,repair_status,pairing_rule,prior_live_rows)
VALUES ('messages','legacy-changed',1,'resolved','test changed history','[]');
UPDATE evaluations SET status='ready', report='{"totalScore":99,"summary":"new evidence"}'::jsonb
WHERE session_id='legacy-new';
INSERT INTO sessions(id,user_id,scenario_id,scenario_name,status,current_round,max_rounds,
  patient_state,evaluation_status,total_score,finished_at)
VALUES ('legacy-live','demo-user-001','implant-basic','Live legacy','completed',1,10,'{}','ready',0,NOW());
INSERT INTO evaluations(session_id,status,report)
VALUES ('legacy-live','ready','{"summary":"zero is valid"}');
'@
  foreach ($rerun in 1..2) {
    Invoke-Psql $historySchema (Join-Path $migrations '009_legacy_report_totals.sql') ''
    Invoke-Psql $historySchema '' @'
DO $$ BEGIN
  IF NOT EXISTS (SELECT 1 FROM evaluations e JOIN sessions s ON s.id=e.session_id
    WHERE s.id='legacy-restore' AND e.status='ready' AND s.total_score=80
      AND e.report->>'summary'='original evidence' AND (e.report->>'totalScore')::int=80
      AND e.model_version='legacy-model' AND e.prompt_version='legacy-prompt'
      AND e.generated_at=TIMESTAMPTZ '2026-01-01 00:00:00+08') THEN
    RAISE EXCEPTION 'legacy archive was not restored without changing evidence/metadata';
  END IF;
  IF (SELECT status FROM ai_jobs WHERE dedupe_key='evaluation:legacy-restore') <> 'succeeded' THEN
    RAISE EXCEPTION 'redundant legacy model job remains claimable';
  END IF;
  IF EXISTS (SELECT 1 FROM evaluations WHERE session_id='legacy-changed' AND status='ready') THEN
    RAISE EXCEPTION 'report based on changed history was restored';
  END IF;
  IF NOT EXISTS (SELECT 1 FROM evaluations WHERE session_id='legacy-new'
    AND report->>'summary'='new evidence' AND (report->>'totalScore')::int=99) THEN
    RAISE EXCEPTION 'replacement report was overwritten';
  END IF;
  IF NOT EXISTS (SELECT 1 FROM evaluations WHERE session_id='legacy-live'
    AND report->>'summary'='zero is valid' AND (report->>'totalScore')::int=0) THEN
    RAISE EXCEPTION 'valid zero session score was lost';
  END IF;
END $$;
'@
  }
  [pscustomobject]@{ Result = 'passed'; EmptySchema = $emptySchema; HistorySchema = $historySchema } |
    ConvertTo-Json -Compress
} finally {
  $env:PGOPTIONS = $previousOptions
  if (-not $KeepSchemas) {
    & $PsqlPath --dbname=$DatabaseUrl -v ON_ERROR_STOP=1 -X -q -c "DROP SCHEMA IF EXISTS $emptySchema CASCADE; DROP SCHEMA IF EXISTS $historySchema CASCADE;"
  }
}
