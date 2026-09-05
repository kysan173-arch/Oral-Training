BEGIN;

-- Run all pending migrations with the backend stopped, before restarting workers.
LOCK TABLE sessions, evaluations, ai_jobs, ai_job_attempts IN SHARE ROW EXCLUSIVE MODE;

-- Prefer a live report. Only restore v005 archives when no replacement report
-- exists and no resolved message repair invalidated the original evidence.
CREATE TEMP TABLE legacy_report_totals ON COMMIT DROP AS
WITH candidates AS (
  SELECT e.session_id, e.report, e.model_version, e.prompt_version, e.generated_at,
         s.total_score AS session_total, false AS restored
  FROM evaluations e JOIN sessions s ON s.id = e.session_id
  WHERE s.status = 'completed' AND e.status = 'ready' AND jsonb_typeof(e.report) = 'object'
  UNION ALL
  SELECT e.session_id, a.source_row->'report', a.source_row->>'model_version',
         a.source_row->>'prompt_version', (a.source_row->>'generated_at')::timestamptz,
         s.total_score, true
  FROM evaluations e JOIN sessions s ON s.id = e.session_id
  JOIN generation_state_repair_archive a ON a.source_table = 'evaluations'
    AND a.source_id = e.session_id AND a.repair_reason = 'regenerate_inconsistent_state_v005'
  WHERE s.status = 'completed' AND e.report IS NULL AND e.status IN ('generating', 'failed')
    AND a.source_row->>'status' = 'ready'
    AND jsonb_typeof(a.source_row->'report') = 'object'
    AND (a.source_row->'report'->'totalScore' IS NULL OR a.source_row->'report'->'totalScore' = 'null'::jsonb)
    AND NOT EXISTS (SELECT 1 FROM message_pair_repair_audit audit
      WHERE audit.source_table = 'messages' AND audit.session_id = s.id AND audit.repair_status = 'resolved')
), scored AS (
  SELECT candidates.*,
    CASE WHEN jsonb_typeof(report->'totalScore') = 'number'
      THEN (report->>'totalScore')::numeric END AS report_total,
    CASE WHEN jsonb_typeof(report->'dimensionScores'->'knowledgeAccuracy') = 'number'
      THEN (report->'dimensionScores'->>'knowledgeAccuracy')::numeric END AS knowledge,
    CASE WHEN jsonb_typeof(report->'dimensionScores'->'medicalCompliance') = 'number'
      THEN (report->'dimensionScores'->>'medicalCompliance')::numeric END AS compliance,
    CASE WHEN jsonb_typeof(report->'dimensionScores'->'empathy') = 'number'
      THEN (report->'dimensionScores'->>'empathy')::numeric END AS empathy,
    CASE WHEN jsonb_typeof(report->'dimensionScores'->'needsDiscovery') = 'number'
      THEN (report->'dimensionScores'->>'needsDiscovery')::numeric END AS needs,
    CASE WHEN jsonb_typeof(report->'dimensionScores'->'serviceEtiquette') = 'number'
      THEN (report->'dimensionScores'->>'serviceEtiquette')::numeric END AS etiquette
  FROM candidates
), resolved AS (
  SELECT scored.*,
    CASE WHEN report_total BETWEEN 0 AND 100 AND trunc(report_total) = report_total THEN report_total
      WHEN session_total BETWEEN 0 AND 100 THEN session_total
      WHEN knowledge BETWEEN 0 AND 100 AND trunc(knowledge) = knowledge
        AND compliance BETWEEN 0 AND 100 AND trunc(compliance) = compliance
        AND empathy BETWEEN 0 AND 100 AND trunc(empathy) = empathy
        AND needs BETWEEN 0 AND 100 AND trunc(needs) = needs
        AND etiquette BETWEEN 0 AND 100 AND trunc(etiquette) = etiquette
      THEN round(knowledge * .25 + compliance * .25 + empathy * .20 + needs * .20 + etiquette * .10)
    END AS total
  FROM scored
)
SELECT * FROM resolved WHERE total IS NOT NULL;

-- Retain the pre-change live row too; archived v005 source rows remain untouched.
INSERT INTO generation_state_repair_archive(source_table, source_id, repair_reason, source_row)
SELECT 'evaluations', e.session_id, 'legacy_total_backfill_v009', to_jsonb(e)
FROM evaluations e JOIN legacy_report_totals r ON r.session_id = e.session_id
WHERE r.restored OR e.report->'totalScore' IS DISTINCT FROM to_jsonb(r.total::int)
ON CONFLICT DO NOTHING;

UPDATE evaluations e SET report = jsonb_set(r.report, '{totalScore}', to_jsonb(r.total::int)),
  status = 'ready', error_type = NULL, model_version = r.model_version,
  prompt_version = r.prompt_version, generated_at = r.generated_at, updated_at = NOW()
FROM legacy_report_totals r WHERE r.session_id = e.session_id
  AND (r.restored OR e.report->'totalScore' IS DISTINCT FROM to_jsonb(r.total::int));

UPDATE sessions s SET evaluation_status = 'ready', total_score = r.total::smallint, updated_at = NOW()
FROM legacy_report_totals r WHERE r.session_id = s.id
  AND (s.evaluation_status <> 'ready' OR s.total_score IS DISTINCT FROM r.total::smallint);

-- Cancel only redundant jobs for restored reports. Marking them succeeded
-- prevents both future claims and an old worker from overwriting recovered data.
INSERT INTO generation_state_repair_archive(source_table, source_id, repair_reason, source_row)
SELECT 'ai_jobs', j.id, 'legacy_total_restore_v009', to_jsonb(j)
FROM ai_jobs j JOIN legacy_report_totals r ON j.dedupe_key = 'evaluation:' || r.session_id
WHERE r.restored
ON CONFLICT DO NOTHING;

UPDATE ai_job_attempts a SET status = 'failed', error_type = 'LEGACY_REPORT_RESTORED',
  error_message = 'original report restored without a model call', finished_at = NOW()
FROM ai_jobs j JOIN legacy_report_totals r ON j.dedupe_key = 'evaluation:' || r.session_id
WHERE r.restored AND a.job_id = j.id AND a.generation = j.generation AND a.status = 'running';

UPDATE ai_jobs j SET status = 'succeeded', lease_until = NULL, worker_id = NULL,
  last_error = NULL, finished_at = NOW(), updated_at = NOW()
FROM legacy_report_totals r WHERE r.restored AND j.dedupe_key = 'evaluation:' || r.session_id;

COMMIT;
