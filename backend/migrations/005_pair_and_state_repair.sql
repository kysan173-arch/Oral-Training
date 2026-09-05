BEGIN;

-- Run this migration while the backend is stopped.  The locks prevent a
-- claimed job from committing a report based on message rows being repaired.
LOCK TABLE messages, roleplay_messages, evaluations, roleplay_summaries, ai_jobs
  IN SHARE ROW EXCLUSIVE MODE;

CREATE TABLE IF NOT EXISTS message_pair_repair_audit (
  id BIGSERIAL PRIMARY KEY,
  source_table TEXT NOT NULL CHECK (source_table IN ('messages', 'roleplay_messages')),
  session_id TEXT NOT NULL,
  round SMALLINT NOT NULL,
  repair_status TEXT NOT NULL CHECK (repair_status IN ('resolved', 'unresolved')),
  chosen_input_id TEXT,
  chosen_reply_id TEXT,
  pairing_rule TEXT NOT NULL,
  prior_live_rows JSONB NOT NULL,
  repaired_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  UNIQUE(source_table, session_id, round)
);

CREATE TABLE IF NOT EXISTS generation_state_repair_archive (
  id BIGSERIAL PRIMARY KEY,
  source_table TEXT NOT NULL
    CHECK (source_table IN ('evaluations', 'roleplay_summaries', 'ai_jobs')),
  source_id TEXT NOT NULL,
  repair_reason TEXT NOT NULL,
  source_row JSONB NOT NULL,
  archived_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  UNIQUE(source_table, source_id, repair_reason)
);

CREATE TEMP TABLE training_pair_groups ON COMMIT DROP AS
SELECT archive.source_row->>'session_id' AS session_id,
       (archive.source_row->>'round')::SMALLINT AS round
FROM message_repair_archive archive
WHERE archive.source_table = 'messages'
  AND archive.repair_reason = 'duplicate_session_role_round'
  AND NOT EXISTS (
    SELECT 1 FROM message_pair_repair_audit audit
    WHERE audit.source_table = 'messages'
      AND audit.session_id = archive.source_row->>'session_id'
      AND audit.round = (archive.source_row->>'round')::SMALLINT
  )
GROUP BY archive.source_row->>'session_id', (archive.source_row->>'round')::SMALLINT;

CREATE TEMP TABLE training_pair_candidates ON COMMIT DROP AS
SELECT message.id, message.session_id, message.role, message.content, message.round,
       message.client_message_id, message.created_at
FROM messages message
JOIN training_pair_groups pair_group
  ON pair_group.session_id = message.session_id AND pair_group.round = message.round
UNION ALL
SELECT archive.source_row->>'id', archive.source_row->>'session_id',
       archive.source_row->>'role', archive.source_row->>'content',
       (archive.source_row->>'round')::SMALLINT,
       archive.source_row->>'client_message_id',
       (archive.source_row->>'created_at')::TIMESTAMPTZ
FROM message_repair_archive archive
JOIN training_pair_groups pair_group
  ON pair_group.session_id = archive.source_row->>'session_id'
 AND pair_group.round = (archive.source_row->>'round')::SMALLINT
WHERE archive.source_table = 'messages'
  AND archive.repair_reason = 'duplicate_session_role_round';

CREATE TEMP TABLE training_repair_pairs ON COMMIT DROP AS
SELECT session_id, round, input_id, reply_id
FROM (
  SELECT reply.session_id, reply.round, input.id AS input_id, reply.id AS reply_id,
         ROW_NUMBER() OVER (
           PARTITION BY reply.session_id, reply.round
           ORDER BY reply.created_at DESC, reply.id DESC
         ) AS pair_rank
  FROM training_pair_candidates reply
  JOIN LATERAL (
    SELECT candidate.id
    FROM training_pair_candidates candidate
    WHERE candidate.session_id = reply.session_id
      AND candidate.round = reply.round
      AND candidate.role = 'user'
      AND candidate.created_at <= reply.created_at
    ORDER BY candidate.created_at DESC, candidate.id DESC
    LIMIT 1
  ) input ON TRUE
  WHERE reply.role = 'patient'
) ranked
WHERE pair_rank = 1;

INSERT INTO message_pair_repair_audit
  (source_table, session_id, round, repair_status, chosen_input_id, chosen_reply_id,
   pairing_rule, prior_live_rows)
SELECT 'messages', pair_group.session_id, pair_group.round,
       CASE WHEN pairs.session_id IS NULL THEN 'unresolved' ELSE 'resolved' END,
       pairs.input_id, pairs.reply_id,
       '选择最新回复，并配对该回复之前创建时间最近的输入；同时间按 ID 降序',
       COALESCE((
         SELECT jsonb_agg(to_jsonb(message) ORDER BY message.created_at, message.id)
         FROM messages message
         WHERE message.session_id = pair_group.session_id AND message.round = pair_group.round
       ), '[]'::JSONB)
FROM training_pair_groups pair_group
LEFT JOIN training_repair_pairs pairs
  ON pairs.session_id = pair_group.session_id AND pairs.round = pair_group.round;

INSERT INTO message_repair_archive(source_table, source_id, repair_reason, source_row)
SELECT 'messages', message.id, 'paired_attempt_repair_v005', to_jsonb(message)
FROM messages message
JOIN training_repair_pairs pairs
  ON pairs.session_id = message.session_id AND pairs.round = message.round
WHERE message.id <> pairs.input_id AND message.id <> pairs.reply_id;

DELETE FROM messages message
USING training_repair_pairs pairs
WHERE message.session_id = pairs.session_id AND message.round = pairs.round
  AND message.id <> pairs.input_id AND message.id <> pairs.reply_id;

INSERT INTO messages
  (id, session_id, role, content, round, client_message_id, created_at,
   reply_status, reply_lease_until, reply_attempt_token, reply_error_type)
SELECT candidate.id, candidate.session_id, candidate.role, candidate.content, candidate.round,
       candidate.client_message_id, candidate.created_at,
       CASE WHEN candidate.role = 'user' THEN 'ready' ELSE NULL END,
       NULL, NULL, NULL
FROM training_pair_candidates candidate
JOIN training_repair_pairs pairs
  ON (candidate.id = pairs.input_id OR candidate.id = pairs.reply_id)
ON CONFLICT (id) DO UPDATE SET
  content = EXCLUDED.content, client_message_id = EXCLUDED.client_message_id,
  created_at = EXCLUDED.created_at, reply_status = EXCLUDED.reply_status,
  reply_lease_until = NULL, reply_attempt_token = NULL, reply_error_type = NULL;

CREATE TEMP TABLE roleplay_pair_groups ON COMMIT DROP AS
SELECT archive.source_row->>'session_id' AS session_id,
       (archive.source_row->>'round')::SMALLINT AS round
FROM message_repair_archive archive
WHERE archive.source_table = 'roleplay_messages'
  AND archive.repair_reason = 'duplicate_session_role_round'
  AND NOT EXISTS (
    SELECT 1 FROM message_pair_repair_audit audit
    WHERE audit.source_table = 'roleplay_messages'
      AND audit.session_id = archive.source_row->>'session_id'
      AND audit.round = (archive.source_row->>'round')::SMALLINT
  )
GROUP BY archive.source_row->>'session_id', (archive.source_row->>'round')::SMALLINT;

CREATE TEMP TABLE roleplay_pair_candidates ON COMMIT DROP AS
SELECT message.id, message.session_id, message.role, message.content,
       message.learning_points, message.compliance_boundary, message.round,
       message.client_message_id, message.created_at
FROM roleplay_messages message
JOIN roleplay_pair_groups pair_group
  ON pair_group.session_id = message.session_id AND pair_group.round = message.round
UNION ALL
SELECT archive.source_row->>'id', archive.source_row->>'session_id',
       archive.source_row->>'role', archive.source_row->>'content',
       COALESCE(archive.source_row->'learning_points', '[]'::JSONB),
       archive.source_row->>'compliance_boundary',
       (archive.source_row->>'round')::SMALLINT,
       archive.source_row->>'client_message_id',
       (archive.source_row->>'created_at')::TIMESTAMPTZ
FROM message_repair_archive archive
JOIN roleplay_pair_groups pair_group
  ON pair_group.session_id = archive.source_row->>'session_id'
 AND pair_group.round = (archive.source_row->>'round')::SMALLINT
WHERE archive.source_table = 'roleplay_messages'
  AND archive.repair_reason = 'duplicate_session_role_round';

CREATE TEMP TABLE roleplay_repair_pairs ON COMMIT DROP AS
SELECT session_id, round, input_id, reply_id
FROM (
  SELECT reply.session_id, reply.round, input.id AS input_id, reply.id AS reply_id,
         ROW_NUMBER() OVER (
           PARTITION BY reply.session_id, reply.round
           ORDER BY reply.created_at DESC, reply.id DESC
         ) AS pair_rank
  FROM roleplay_pair_candidates reply
  JOIN LATERAL (
    SELECT candidate.id
    FROM roleplay_pair_candidates candidate
    WHERE candidate.session_id = reply.session_id
      AND candidate.round = reply.round
      AND candidate.role = 'learner_patient'
      AND candidate.created_at <= reply.created_at
    ORDER BY candidate.created_at DESC, candidate.id DESC
    LIMIT 1
  ) input ON TRUE
  WHERE reply.role = 'standard_customer'
) ranked
WHERE pair_rank = 1;

INSERT INTO message_pair_repair_audit
  (source_table, session_id, round, repair_status, chosen_input_id, chosen_reply_id,
   pairing_rule, prior_live_rows)
SELECT 'roleplay_messages', pair_group.session_id, pair_group.round,
       CASE WHEN pairs.session_id IS NULL THEN 'unresolved' ELSE 'resolved' END,
       pairs.input_id, pairs.reply_id,
       '选择最新回复，并配对该回复之前创建时间最近的输入；同时间按 ID 降序',
       COALESCE((
         SELECT jsonb_agg(to_jsonb(message) ORDER BY message.created_at, message.id)
         FROM roleplay_messages message
         WHERE message.session_id = pair_group.session_id AND message.round = pair_group.round
       ), '[]'::JSONB)
FROM roleplay_pair_groups pair_group
LEFT JOIN roleplay_repair_pairs pairs
  ON pairs.session_id = pair_group.session_id AND pairs.round = pair_group.round;

INSERT INTO message_repair_archive(source_table, source_id, repair_reason, source_row)
SELECT 'roleplay_messages', message.id, 'paired_attempt_repair_v005', to_jsonb(message)
FROM roleplay_messages message
JOIN roleplay_repair_pairs pairs
  ON pairs.session_id = message.session_id AND pairs.round = message.round
WHERE message.id <> pairs.input_id AND message.id <> pairs.reply_id;

DELETE FROM roleplay_messages message
USING roleplay_repair_pairs pairs
WHERE message.session_id = pairs.session_id AND message.round = pairs.round
  AND message.id <> pairs.input_id AND message.id <> pairs.reply_id;

INSERT INTO roleplay_messages
  (id, session_id, role, content, learning_points, compliance_boundary, round,
   client_message_id, created_at, reply_status, reply_lease_until,
   reply_attempt_token, reply_error_type)
SELECT candidate.id, candidate.session_id, candidate.role, candidate.content,
       candidate.learning_points, candidate.compliance_boundary, candidate.round,
       candidate.client_message_id, candidate.created_at,
       CASE WHEN candidate.role = 'learner_patient' THEN 'ready' ELSE NULL END,
       NULL, NULL, NULL
FROM roleplay_pair_candidates candidate
JOIN roleplay_repair_pairs pairs
  ON (candidate.id = pairs.input_id OR candidate.id = pairs.reply_id)
ON CONFLICT (id) DO UPDATE SET
  content = EXCLUDED.content, learning_points = EXCLUDED.learning_points,
  compliance_boundary = EXCLUDED.compliance_boundary,
  client_message_id = EXCLUDED.client_message_id, created_at = EXCLUDED.created_at,
  reply_status = EXCLUDED.reply_status, reply_lease_until = NULL,
  reply_attempt_token = NULL, reply_error_type = NULL;

-- Reconcile valid reports first, and preserve explicit failures.  Everything
-- else is queued only when a report/job is missing or a repaired pair changed
-- the history on which an existing report was based.
CREATE TEMP TABLE valid_evaluation_reports ON COMMIT DROP AS
SELECT parsed.session_id, parsed.total_score
FROM (
  SELECT evaluation.session_id,
         CASE
           WHEN evaluation.status = 'ready'
            AND jsonb_typeof(evaluation.report) = 'object'
            AND jsonb_typeof(evaluation.report->'totalScore') = 'number'
            AND (evaluation.report->>'totalScore') ~ '^[0-9]+$'
           THEN (evaluation.report->>'totalScore')::INTEGER
           ELSE NULL
         END AS total_score
  FROM evaluations evaluation
) parsed
WHERE parsed.total_score BETWEEN 0 AND 100;

UPDATE sessions session
SET evaluation_status = 'ready',
    total_score = valid.total_score::SMALLINT,
    updated_at = NOW()
FROM valid_evaluation_reports valid
WHERE valid.session_id = session.id
  AND session.status = 'completed'
  AND NOT EXISTS (
    SELECT 1 FROM training_repair_pairs pairs WHERE pairs.session_id = session.id
  );

INSERT INTO evaluations(session_id, status, error_type, updated_at)
SELECT session.id, 'failed', 'STATE_RECORD_MISSING', NOW()
FROM sessions session
LEFT JOIN evaluations evaluation ON evaluation.session_id = session.id
WHERE session.status = 'completed' AND session.evaluation_status = 'failed'
  AND evaluation.session_id IS NULL
ON CONFLICT (session_id) DO NOTHING;

UPDATE sessions session
SET evaluation_status = 'failed', total_score = NULL, updated_at = NOW()
FROM evaluations evaluation
WHERE evaluation.session_id = session.id AND session.status = 'completed'
  AND evaluation.status = 'failed'
  AND NOT EXISTS (
    SELECT 1 FROM training_repair_pairs pairs WHERE pairs.session_id = session.id
  );

CREATE TEMP TABLE evaluation_repair_targets ON COMMIT DROP AS
SELECT session.id,
       EXISTS (SELECT 1 FROM training_repair_pairs pairs WHERE pairs.session_id = session.id)
         AS force_reset
FROM sessions session
LEFT JOIN evaluations evaluation ON evaluation.session_id = session.id
LEFT JOIN ai_jobs job ON job.dedupe_key = 'evaluation:' || session.id
WHERE session.status = 'completed'
  AND (
    EXISTS (SELECT 1 FROM training_repair_pairs pairs WHERE pairs.session_id = session.id)
    OR (session.evaluation_status <> 'failed' AND (
      evaluation.session_id IS NULL
      OR evaluation.status = 'ready' AND NOT EXISTS (
        SELECT 1 FROM valid_evaluation_reports valid WHERE valid.session_id = session.id
      )
      OR evaluation.status = 'generating'
        AND (job.id IS NULL OR job.status IN ('succeeded', 'dead'))
    ))
  );

INSERT INTO generation_state_repair_archive(source_table, source_id, repair_reason, source_row)
SELECT 'evaluations', evaluation.session_id, 'regenerate_inconsistent_state_v005',
       to_jsonb(evaluation)
FROM evaluations evaluation
JOIN evaluation_repair_targets target ON target.id = evaluation.session_id
ON CONFLICT DO NOTHING;

INSERT INTO generation_state_repair_archive(source_table, source_id, repair_reason, source_row)
SELECT 'ai_jobs', job.id, 'regenerate_inconsistent_state_v005', to_jsonb(job)
FROM ai_jobs job
JOIN evaluation_repair_targets target ON job.dedupe_key = 'evaluation:' || target.id
WHERE target.force_reset OR job.status IN ('succeeded', 'dead')
ON CONFLICT DO NOTHING;

UPDATE ai_job_attempts attempt
SET status = 'failed', error_type = 'HISTORY_PAIR_REPAIRED',
    error_message = 'message history changed during migration 005', finished_at = NOW()
FROM ai_jobs job, evaluation_repair_targets target
WHERE target.force_reset AND job.dedupe_key = 'evaluation:' || target.id
  AND attempt.job_id = job.id AND attempt.generation = job.generation
  AND attempt.status = 'running';

INSERT INTO evaluations(session_id, status, report, error_type, updated_at)
SELECT id, 'generating', NULL, NULL, NOW() FROM evaluation_repair_targets
ON CONFLICT (session_id) DO UPDATE SET status = 'generating', report = NULL,
  error_type = NULL, generated_at = NULL, updated_at = NOW();

UPDATE sessions session
SET evaluation_status = 'generating', total_score = NULL, updated_at = NOW()
FROM evaluation_repair_targets target
WHERE target.id = session.id;

UPDATE ai_jobs job
SET status = 'pending', generation = job.generation + 1, attempts = 0,
  available_at = NOW(), lease_until = NULL, worker_id = NULL,
  last_error = NULL, finished_at = NULL, updated_at = NOW()
FROM evaluation_repair_targets target
WHERE job.dedupe_key = 'evaluation:' || target.id
  AND (target.force_reset OR job.status IN ('succeeded', 'dead'))
  AND job.generation < 100;

INSERT INTO ai_jobs
  (id, job_type, target_id, dedupe_key, status, attempts, available_at, updated_at)
SELECT 'job_eval_repair_' || md5(target.id), 'evaluation', target.id,
       'evaluation:' || target.id, 'pending', 0, NOW(), NOW()
FROM evaluation_repair_targets target
ON CONFLICT (dedupe_key) DO NOTHING;

UPDATE evaluations evaluation
SET status = 'failed', report = NULL, error_type = 'JOB_GENERATION_EXHAUSTED', updated_at = NOW()
FROM ai_jobs job, evaluation_repair_targets target
WHERE target.id = evaluation.session_id
  AND job.dedupe_key = 'evaluation:' || target.id
  AND job.status IN ('succeeded', 'dead') AND job.generation = 100;

UPDATE sessions session
SET evaluation_status = 'failed', total_score = NULL, updated_at = NOW()
FROM evaluations evaluation
WHERE evaluation.session_id = session.id
  AND evaluation.status = 'failed' AND evaluation.error_type = 'JOB_GENERATION_EXHAUSTED';

CREATE TEMP TABLE summary_repair_targets ON COMMIT DROP AS
SELECT session.id,
       EXISTS (SELECT 1 FROM roleplay_repair_pairs pairs WHERE pairs.session_id = session.id)
         AS force_reset
FROM roleplay_sessions session
LEFT JOIN roleplay_summaries summary ON summary.session_id = session.id
LEFT JOIN ai_jobs job ON job.dedupe_key = 'roleplay-summary:' || session.id
WHERE session.status = 'completed'
  AND (
    EXISTS (SELECT 1 FROM roleplay_repair_pairs pairs WHERE pairs.session_id = session.id)
    OR summary.session_id IS NULL
    OR summary.status = 'ready'
      AND (summary.summary IS NULL OR jsonb_typeof(summary.summary) <> 'object')
    OR summary.status = 'generating'
      AND (job.id IS NULL OR job.status IN ('succeeded', 'dead'))
  );

INSERT INTO generation_state_repair_archive(source_table, source_id, repair_reason, source_row)
SELECT 'roleplay_summaries', summary.session_id, 'regenerate_inconsistent_state_v005',
       to_jsonb(summary)
FROM roleplay_summaries summary
JOIN summary_repair_targets target ON target.id = summary.session_id
ON CONFLICT DO NOTHING;

INSERT INTO generation_state_repair_archive(source_table, source_id, repair_reason, source_row)
SELECT 'ai_jobs', job.id, 'regenerate_inconsistent_state_v005', to_jsonb(job)
FROM ai_jobs job
JOIN summary_repair_targets target ON job.dedupe_key = 'roleplay-summary:' || target.id
WHERE target.force_reset OR job.status IN ('succeeded', 'dead')
ON CONFLICT DO NOTHING;

UPDATE ai_job_attempts attempt
SET status = 'failed', error_type = 'HISTORY_PAIR_REPAIRED',
    error_message = 'message history changed during migration 005', finished_at = NOW()
FROM ai_jobs job, summary_repair_targets target
WHERE target.force_reset AND job.dedupe_key = 'roleplay-summary:' || target.id
  AND attempt.job_id = job.id AND attempt.generation = job.generation
  AND attempt.status = 'running';

INSERT INTO roleplay_summaries(session_id, status, summary, error_type, updated_at)
SELECT id, 'generating', NULL, NULL, NOW() FROM summary_repair_targets
ON CONFLICT (session_id) DO UPDATE SET status = 'generating', summary = NULL,
  error_type = NULL, generated_at = NULL, updated_at = NOW();

UPDATE ai_jobs job
SET status = 'pending', generation = job.generation + 1, attempts = 0,
  available_at = NOW(), lease_until = NULL, worker_id = NULL,
  last_error = NULL, finished_at = NULL, updated_at = NOW()
FROM summary_repair_targets target
WHERE job.dedupe_key = 'roleplay-summary:' || target.id
  AND (target.force_reset OR job.status IN ('succeeded', 'dead'))
  AND job.generation < 100;

INSERT INTO ai_jobs
  (id, job_type, target_id, dedupe_key, status, attempts, available_at, updated_at)
SELECT 'job_summary_repair_' || md5(target.id), 'roleplay_summary', target.id,
       'roleplay-summary:' || target.id, 'pending', 0, NOW(), NOW()
FROM summary_repair_targets target
ON CONFLICT (dedupe_key) DO NOTHING;

UPDATE roleplay_summaries summary
SET status = 'failed', summary = NULL, error_type = 'JOB_GENERATION_EXHAUSTED', updated_at = NOW()
FROM ai_jobs job, summary_repair_targets target
WHERE target.id = summary.session_id
  AND job.dedupe_key = 'roleplay-summary:' || target.id
  AND job.status IN ('succeeded', 'dead') AND job.generation = 100;

COMMIT;
