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
  Invoke-Psql $emptySchema (Join-Path $migrations '010_knowledge_catalog.sql') ''
  Invoke-Psql $emptySchema (Join-Path $migrations '011_roleplay_rag_mvp.sql') ''
  Invoke-Psql $emptySchema (Join-Path $migrations '012_custom_patient_profile.sql') ''
  Invoke-Psql $emptySchema (Join-Path $migrations '013_recommendation_scenario.sql') ''
  Invoke-Psql $emptySchema (Join-Path $migrations '014_training_plans.sql') ''
  Invoke-Psql $emptySchema (Join-Path $migrations '015_supervisor_team.sql') ''
  Invoke-Psql $emptySchema (Join-Path $migrations '016_message_emotion.sql') ''
  Invoke-Psql $emptySchema (Join-Path $migrations '017_hint_per_round.sql') ''
  Invoke-Psql $emptySchema (Join-Path $migrations '018_scenario_reaction_rules.sql') ''
  Invoke-Psql $emptySchema (Join-Path $migrations '019_roleplay_free_template.sql') ''
  Invoke-Psql $emptySchema (Join-Path $migrations '020_conflict_scenarios.sql') ''
  Invoke-Psql $emptySchema (Join-Path $migrations '021_ai_training_plans.sql') ''
  Invoke-Psql $emptySchema (Join-Path $migrations '022_plan_focus_dimension.sql') ''
  Invoke-Psql $emptySchema (Join-Path $migrations '023_plan_scenario_cap.sql') ''
  Invoke-Psql $emptySchema (Join-Path $migrations '024_scenario_dimension_weights.sql') ''
  Invoke-Psql $emptySchema (Join-Path $migrations '025_scenario_templates.sql') ''
  Invoke-Psql $emptySchema (Join-Path $migrations '026_difficulty_tiers.sql') ''
  Invoke-Psql $emptySchema (Join-Path $migrations '027_advanced_tier_openings.sql') ''
  Invoke-Psql $emptySchema (Join-Path $migrations '028_scenario_variants.sql') ''
  Invoke-Psql $emptySchema (Join-Path $migrations '029_scenario_variants_bulk.sql') ''
  Invoke-Psql $emptySchema (Join-Path $migrations '030_scenario_ai_draft.sql') ''
  Invoke-Psql $emptySchema '' @'
DO $$ BEGIN
  IF to_regclass('message_repair_archive') IS NULL OR to_regclass('ai_jobs') IS NULL OR
     to_regclass('users') IS NULL OR to_regclass('auth_sessions') IS NULL OR
     to_regclass('message_pair_repair_audit') IS NULL OR
     to_regclass('generation_state_repair_archive') IS NULL OR
     to_regclass('learner_mistake_progress') IS NULL OR to_regclass('session_hints') IS NULL OR
     to_regclass('learner_checkins') IS NULL OR to_regclass('learner_phrase_favorites') IS NULL OR
     to_regclass('training_plans') IS NULL OR to_regclass('training_assignments') IS NULL OR
     to_regclass('supervisor_team_members') IS NULL OR
     to_regclass('clinic_services') IS NULL OR to_regclass('service_revisions') IS NULL OR
     to_regclass('knowledge_entries') IS NULL OR to_regclass('knowledge_revisions') IS NULL OR
     to_regclass('knowledge_chunks') IS NULL OR to_regclass('knowledge_admin_jobs') IS NULL OR
     to_regclass('knowledge_audit_events') IS NULL THEN
    RAISE EXCEPTION 'empty database migration did not create required tables';
  END IF;
  IF NOT EXISTS (
    SELECT 1 FROM information_schema.columns
    WHERE table_name = 'messages' AND column_name = 'emotion'
  ) THEN
    RAISE EXCEPTION 'message emotion column was not created';
  END IF;
  IF NOT EXISTS (
    SELECT 1 FROM information_schema.columns
    WHERE table_name = 'session_hints' AND column_name = 'round' AND is_nullable = 'NO'
  ) THEN
    RAISE EXCEPTION 'session_hints.round was not created as NOT NULL';
  END IF;
  IF NOT EXISTS (
    SELECT 1 FROM pg_constraint
    WHERE conname = 'session_hints_session_id_round_key' AND conrelid = 'session_hints'::regclass
  ) THEN
    RAISE EXCEPTION 'session_hints lost its per-round uniqueness key';
  END IF;
  -- hint_number must already be uncapped from 1..3: the hint index is the
  -- session-wide sequence, not the round, so the old cap made later rounds
  IF EXISTS (
    SELECT 1 FROM pg_constraint
    WHERE conname = 'session_hints_hint_number_check' AND conrelid = 'session_hints'::regclass
  ) THEN
    RAISE EXCEPTION 'session_hints still caps hint_number at 3';
  END IF;
  -- max_per_scenario is the only anti-farming switch: a missing column breaks
  -- all four plan progress queries, and a missing CHECK lets any cap
  IF NOT EXISTS (
    SELECT 1 FROM information_schema.columns
    WHERE table_name = 'training_plans' AND column_name = 'max_per_scenario'
      AND is_nullable = 'NO' AND column_default LIKE '0%'
  ) THEN
    RAISE EXCEPTION 'training_plans.max_per_scenario was not created as NOT NULL DEFAULT 0';
  END IF;
  IF NOT EXISTS (
    SELECT 1 FROM pg_constraint
    WHERE conname = 'training_plans_max_per_scenario_check'
      AND conrelid = 'training_plans'::regclass
  ) THEN
    RAISE EXCEPTION 'training_plans lost its max_per_scenario CHECK';
  END IF;
  -- AI scenario skeleton draft (030). Three things must be asserted:
  --   * kind CHECK must accept scenario_draft, or the queue cannot insert the job;
  --   * generation_id is both the in-progress marker and the optimistic
  --     concurrency gate: non-null means that job owns the placeholder row;
  --   * ai_draft is a provenance marker and must be NOT NULL DEFAULT FALSE,
  --     or legacy rows would be NULL and the list badge becomes tri-state.
  -- Note: a boolean default renders bare in information_schema (no quotes),
  -- unlike jsonb which renders as ''{}''::jsonb -- easy to copy wrong.
  IF NOT EXISTS (
    SELECT 1 FROM pg_constraint
    WHERE conname = 'knowledge_admin_jobs_kind_check'
      AND conrelid = 'knowledge_admin_jobs'::regclass
      AND pg_get_constraintdef(oid) LIKE '%scenario_draft%'
  ) THEN
    RAISE EXCEPTION 'knowledge_admin_jobs.kind still rejects scenario_draft';
  END IF;
  IF NOT EXISTS (
    SELECT 1 FROM information_schema.columns
    WHERE table_name = 'scenarios' AND column_name = 'generation_id'
  ) THEN
    RAISE EXCEPTION 'scenarios.generation_id was not created';
  END IF;
  IF NOT EXISTS (
    SELECT 1 FROM information_schema.columns
    WHERE table_name = 'scenarios' AND column_name = 'ai_draft'
      AND is_nullable = 'NO' AND column_default LIKE 'false%'
  ) THEN
    RAISE EXCEPTION 'scenarios.ai_draft was not created as NOT NULL DEFAULT FALSE';
  END IF;
  -- An in-progress scenario must not be publishable (DB-level guard against
  -- hand-written SQL pushing a half-finished skeleton to learners).
  IF NOT EXISTS (
    SELECT 1 FROM pg_constraint
    WHERE conname = 'scenarios_generating_not_active_check'
      AND conrelid = 'scenarios'::regclass
  ) THEN
    RAISE EXCEPTION 'scenarios lost its generating-not-active guard';
  END IF;
  -- scenario dimension weights (024): the column is the whole point of the migration,
  -- so assert the column shape AND that the backfill actually produced usable data.
  -- NOTE the quoting: information_schema renders a jsonb default as `'{}'::jsonb`, i.e.
  -- it STARTS with a quote. `LIKE '{}%'` therefore never matches and the assertion would
  -- always fire — this script has a database-name guard that keeps it from running
  -- casually, so the mistake sat here unreported until the block was executed by hand.
  IF NOT EXISTS (
    SELECT 1 FROM information_schema.columns
    WHERE table_name = 'scenarios' AND column_name = 'dimension_weights'
      AND is_nullable = 'NO' AND column_default LIKE '''{}''%'
  ) THEN
    RAISE EXCEPTION 'scenarios.dimension_weights was not created as NOT NULL DEFAULT {}';
  END IF;
  IF NOT EXISTS (
    SELECT 1 FROM pg_constraint
    WHERE conname = 'scenarios_dimension_weights_check'
      AND conrelid = 'scenarios'::regclass
  ) THEN
    RAISE EXCEPTION 'scenarios lost its dimension_weights CHECK';
  END IF;
  -- A named scenario must have been backfilled: proves the UPDATEs matched, which a
  -- bare column-exists check would not catch (e.g. a typo'd id would pass silently).
  IF NOT EXISTS (
    SELECT 1 FROM scenarios
    WHERE id = 'guarantee-demand' AND dimension_weights <> '{}'::jsonb
  ) THEN
    RAISE EXCEPTION 'scenario dimension weights were not backfilled';
  END IF;
  -- Every stored weight must be a positive number <= 1. Guards the invariant the
  -- recommendation path relies on when it orders scenarios by a single weight.
  IF EXISTS (
    SELECT 1 FROM scenarios, jsonb_each(dimension_weights) AS w(key, value)
    WHERE jsonb_typeof(w.value) <> 'number'
       OR (w.value)::numeric <= 0 OR (w.value)::numeric > 1
  ) THEN
    RAISE EXCEPTION 'scenario dimension weights contain values outside (0, 1]';
  END IF;
  -- Only the five scoring dimensions may appear as keys.
  IF EXISTS (
    SELECT 1 FROM scenarios, jsonb_object_keys(dimension_weights) AS key
    WHERE key NOT IN ('knowledgeAccuracy', 'medicalCompliance', 'empathy',
                      'needsDiscovery', 'serviceEtiquette')
  ) THEN
    RAISE EXCEPTION 'scenario dimension weights contain unknown dimension keys';
  END IF;
  -- Scenario skeleton templates (025). These rows are only useful if they can actually be
  -- cloned: createScenario re-validates every field it inherits from the template, so one
  -- over-long field here makes "create from template" fail with a 400 at runtime. Hence
  -- asserting every field length against the payload rules, not just the row count.
  IF (SELECT COUNT(*) FROM scenarios WHERE is_template AND id LIKE 'tpl-%') <> 8 THEN
    RAISE EXCEPTION 'expected eight scenario skeleton templates';
  END IF;
  IF EXISTS (
    SELECT 1 FROM scenarios
    WHERE is_template AND id LIKE 'tpl-%'
      AND (is_active OR char_length(name) NOT BETWEEN 2 AND 30
           OR char_length(summary) NOT BETWEEN 2 AND 60
           OR char_length(patient_profile->>'description') NOT BETWEEN 2 AND 60
           OR char_length(hidden_config->>'opening') NOT BETWEEN 5 AND 200
           OR char_length(hidden_config->>'instructions') NOT BETWEEN 5 AND 400
           OR jsonb_array_length(focus) NOT BETWEEN 1 AND 6
           OR jsonb_array_length(hidden_config->'hidden') < 1
           OR sort_order NOT BETWEEN 901 AND 999)
  ) THEN
    RAISE EXCEPTION 'scenario skeleton templates would fail payload validation';
  END IF;
  -- Templates must never reach the learner list; is_active=FALSE is the second guard
  -- behind the is_template filter, so assert it stayed FALSE.
  IF EXISTS (SELECT 1 FROM scenarios WHERE id LIKE 'tpl-%' AND is_active) THEN
    RAISE EXCEPTION 'scenario skeleton templates leaked into the learner-visible set';
  END IF;
  -- Difficulty tiers (026). The learner picks a tier explicitly, the session records which
  -- one was used, and the plan-progress SQL excludes the advanced tier so nobody is
  -- penalised for taking on extra difficulty. Assert both columns and their constraints.
  IF NOT EXISTS (
    SELECT 1 FROM information_schema.columns
    WHERE table_name = 'sessions' AND column_name = 'difficulty_tier'
      AND is_nullable = 'NO' AND column_default LIKE '''standard''%'
  ) THEN
    RAISE EXCEPTION 'sessions.difficulty_tier was not created as NOT NULL DEFAULT standard';
  END IF;
  IF NOT EXISTS (
    SELECT 1 FROM pg_constraint
    WHERE conname = 'sessions_difficulty_tier_check' AND conrelid = 'sessions'::regclass
  ) THEN
    RAISE EXCEPTION 'sessions lost its difficulty_tier CHECK';
  END IF;
  IF NOT EXISTS (
    SELECT 1 FROM pg_constraint
    WHERE conname = 'scenarios_difficulty_tiers_check' AND conrelid = 'scenarios'::regclass
  ) THEN
    RAISE EXCEPTION 'scenarios lost its difficulty_tiers CHECK';
  END IF;
  -- Every cloned-from template must carry an advanced tier, otherwise "create from
  -- template" silently produces a scenario that can never be challenged.
  IF EXISTS (SELECT 1 FROM scenarios WHERE id LIKE 'tpl-%' AND NOT (difficulty_tiers ? 'advanced')) THEN
    RAISE EXCEPTION 'scenario skeleton templates were not given an advanced tier';
  END IF;
  -- And the tier must actually be HARDER: lower trust, not-weaker emotion, known emotion
  -- vocabulary. A tier that is not harder is worse than no tier at all, because the
  -- learner believes they challenged themselves when they did not.
  IF EXISTS (
    SELECT 1 FROM scenarios
    WHERE difficulty_tiers ? 'advanced' AND (
      (difficulty_tiers->'advanced'->'initialState'->>'trustLevel')::int >=
        COALESCE((hidden_config->'initialState'->>'trustLevel')::int, 50)
      OR (difficulty_tiers->'advanced'->'initialState'->>'emotionLevel')::int >
        COALESCE((hidden_config->'initialState'->>'emotionLevel')::int, 0)
      OR COALESCE(difficulty_tiers->'advanced'->'initialState'->>'emotion', '') NOT IN
        ('平静', '犹豫', '焦虑', '缓和', '不满', '愤怒')
    )
  ) THEN
    RAISE EXCEPTION 'advanced difficulty tier is not actually harder than standard';
  END IF;
  -- 027: an advanced tier must carry its OWN opening lines. 026 only overrode the initial
  -- state, so the patient's first sentence was still the standard-tier one — the learner
  -- could not tell the two tiers apart, and a tier you cannot feel is worse than no tier.
  IF EXISTS (
    SELECT 1 FROM scenarios
    WHERE NOT is_template AND difficulty_tiers ? 'advanced'
      AND NOT (difficulty_tiers->'advanced' ? 'openings')
  ) THEN
    RAISE EXCEPTION 'scenario advanced tier is missing its own opening lines';
  END IF;
  -- Variants are the point: one fixed line gets memorised by the third attempt and the
  -- score stops meaning anything. Requiring >= 2 keeps that property from silently rotting.
  IF EXISTS (
    SELECT 1 FROM scenarios
    WHERE difficulty_tiers->'advanced' ? 'openings'
      AND (jsonb_typeof(difficulty_tiers->'advanced'->'openings') <> 'array'
           OR jsonb_array_length(difficulty_tiers->'advanced'->'openings') < 2)
  ) THEN
    RAISE EXCEPTION 'advanced tier openings must be an array of at least 2 variants';
  END IF;
  -- Each line must be a usable opening (5-200 chars) and must NOT be a copy of the
  -- standard-tier sentence — that copy is exactly the bug 027 fixes.
  IF EXISTS (
    SELECT 1 FROM scenarios,
      jsonb_array_elements_text(difficulty_tiers->'advanced'->'openings') AS line
    WHERE difficulty_tiers->'advanced' ? 'openings'
      AND (char_length(line) NOT BETWEEN 5 AND 200 OR btrim(line) = ''
           OR line = hidden_config->>'opening')
  ) THEN
    RAISE EXCEPTION 'advanced tier openings contain an empty, oversized or duplicated line';
  END IF;
  -- 028 + 029: variant pool. Every active non-template scenario must carry >= 2 variants;
  -- a variant whose hidden set equals the main value (or another variant) is pointless, so
  -- assert distinctness rather than mere presence. 028 seeded 2 demo scenarios, 029 covered
  -- the remaining 8 — a partial rollout would silently leave most scenarios memorisable.
  IF EXISTS (
    SELECT 1 FROM scenarios
    WHERE is_active AND NOT is_template
      AND (jsonb_typeof(hidden_config->'variants') <> 'array'
           OR jsonb_array_length(hidden_config->'variants') < 2)
  ) THEN
    RAISE EXCEPTION 'active scenarios must all define at least 2 variants';
  END IF;
  IF EXISTS (
    SELECT 1 FROM scenarios,
      jsonb_array_elements(hidden_config->'variants') AS v
    WHERE is_active AND NOT is_template
      AND (NOT (v ? 'hidden') OR jsonb_typeof(v->'hidden') <> 'array'
           OR jsonb_array_length(v->'hidden') = 0
           OR v->'hidden' = hidden_config->'hidden')
  ) THEN
    RAISE EXCEPTION 'scenario variants must override hidden with a distinct non-empty set';
  END IF;
END $$;
'@

  Invoke-Psql $historySchema $fixture ''
  Invoke-Psql $historySchema (Join-Path $migrations '003_reliability.sql') ''
  Invoke-Psql $historySchema (Join-Path $migrations '004_identity.sql') ''
  Invoke-Psql $historySchema (Join-Path $migrations '006_learner_insights.sql') ''
  Invoke-Psql $historySchema (Join-Path $migrations '007_training_experience.sql') ''
  Invoke-Psql $historySchema (Join-Path $migrations '008_supervisor_growth.sql') ''
  Invoke-Psql $historySchema (Join-Path $migrations '010_knowledge_catalog.sql') ''
  Invoke-Psql $historySchema (Join-Path $migrations '012_custom_patient_profile.sql') ''
  Invoke-Psql $historySchema (Join-Path $migrations '013_recommendation_scenario.sql') ''
  Invoke-Psql $historySchema (Join-Path $migrations '014_training_plans.sql') ''
  Invoke-Psql $historySchema (Join-Path $migrations '015_supervisor_team.sql') ''
  Invoke-Psql $historySchema (Join-Path $migrations '016_message_emotion.sql') ''
  Invoke-Psql $historySchema (Join-Path $migrations '017_hint_per_round.sql') ''
  Invoke-Psql $historySchema '' @'
INSERT INTO learner_mistake_progress(user_id, session_id, mistake_key, mastered_at)
VALUES ('demo-user-001', 'test-max-rounds', 'fixture-mistake', NOW());
INSERT INTO session_hints(id, session_id, hint_number, round, content)
VALUES ('fixture-hint', 'test-max-rounds', 1, 1, 'Confirm the concern before explaining the clinical assessment boundary.');
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

  # Legacy rows written before 017 have no round.  They must be bound to a real
  # round of their own session, not silently dropped or duplicated -- and the
  # (session_id, round) key has to survive the backfill.
  Invoke-Psql $historySchema '' @'
INSERT INTO session_hints(id, session_id, hint_number, content)
VALUES ('fixture-legacy-hint', 'test-max-rounds', 1, 'Legacy hint without a round column value.');
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
  Invoke-Psql $historySchema (Join-Path $migrations '010_knowledge_catalog.sql') ''
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
  Invoke-Psql $historySchema (Join-Path $migrations '006_learner_insights.sql') ''
  Invoke-Psql $historySchema (Join-Path $migrations '007_training_experience.sql') ''
  Invoke-Psql $historySchema (Join-Path $migrations '008_supervisor_growth.sql') ''
  Invoke-Psql $historySchema (Join-Path $migrations '012_custom_patient_profile.sql') ''
  Invoke-Psql $historySchema (Join-Path $migrations '013_recommendation_scenario.sql') ''
  Invoke-Psql $historySchema (Join-Path $migrations '014_training_plans.sql') ''
  Invoke-Psql $historySchema (Join-Path $migrations '015_supervisor_team.sql') ''
  Invoke-Psql $historySchema (Join-Path $migrations '016_message_emotion.sql') ''
  Invoke-Psql $historySchema (Join-Path $migrations '017_hint_per_round.sql') ''
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
    WHERE id = 'fixture-hint' AND session_id = 'test-max-rounds'
      AND hint_number = 1 AND round = 1
  ) THEN
    RAISE EXCEPTION 'training hint was not preserved on migration rerun';
  END IF;
  IF NOT EXISTS (
    SELECT 1 FROM session_hints
    WHERE id = 'fixture-legacy-hint' AND session_id = 'test-max-rounds' AND round IS NOT NULL
  ) THEN
    RAISE EXCEPTION 'legacy hint was not backfilled with a round';
  END IF;
  IF (SELECT COUNT(*) FROM session_hints WHERE session_id = 'test-max-rounds') <> 2 THEN
    RAISE EXCEPTION 'hint backfill changed the number of stored hints';
  END IF;
  IF NOT EXISTS (
    SELECT 1 FROM pg_constraint
    WHERE conname = 'session_hints_session_id_round_key' AND conrelid = 'session_hints'::regclass
  ) THEN
    RAISE EXCEPTION 'per-round hint uniqueness key was lost on migration rerun';
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

  # 015 backfill must only happen on first install. Build the "exactly one active
  # supervisor + one active learner" state first, then rerun the migrations: if the
  # first_install guard regresses, the learner gets pushed back under the supervisor
  # and a manually removed member silently comes back to life.
  # NOTE: keep every line of this file ASCII. PowerShell 5.1 reads .ps1 as the ANSI
  # codepage (GBK here), so UTF-8 CJK comment bytes get mis-decoded and can swallow
  # the following newline -- which folded the here-string opener on the next line
  # into the comment and made the whole script unparseable (50 syntax errors, and
  # it still parsed as broken on HEAD). ASCII comments only.
  Invoke-Psql $historySchema '' @'
INSERT INTO users(id, display_name, role, status, is_demo)
VALUES ('reliability-supervisor', 'Reliability Supervisor', 'admin', 'active', TRUE),
       ('reliability-learner', 'Reliability Learner', 'learner', 'active', TRUE)
ON CONFLICT (id) DO NOTHING;
'@
  Invoke-Psql $historySchema (Join-Path $migrations '015_supervisor_team.sql') ''
  Invoke-Psql $historySchema (Join-Path $migrations '016_message_emotion.sql') ''
  Invoke-Psql $historySchema (Join-Path $migrations '017_hint_per_round.sql') ''
  Invoke-Psql $historySchema '' @'
DO $$ BEGIN
  IF to_regclass('supervisor_team_members') IS NULL THEN
    RAISE EXCEPTION 'supervisor team table vanished on migration rerun';
  END IF;
  IF (SELECT COUNT(*) FROM supervisor_team_members) <> 0 THEN
    RAISE EXCEPTION 'migration rerun resurrected team membership a supervisor had removed';
  END IF;
  IF NOT EXISTS (
    SELECT 1 FROM information_schema.table_constraints
    WHERE table_name = 'supervisor_team_members' AND constraint_type = 'PRIMARY KEY'
      AND constraint_name = 'supervisor_team_members_pkey'
  ) THEN
    RAISE EXCEPTION 'supervisor team table lost its learner_id primary key';
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
