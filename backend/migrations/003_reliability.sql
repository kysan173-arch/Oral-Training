BEGIN;

-- Keep every row removed while repairing historical duplicate rounds.  The
-- JSON snapshot deliberately includes all source columns so a repair can be
-- audited or reversed without relying on the current table shape.
CREATE TABLE IF NOT EXISTS message_repair_archive (
  id BIGSERIAL PRIMARY KEY,
  source_table TEXT NOT NULL CHECK (source_table IN ('messages', 'roleplay_messages')),
  source_id TEXT NOT NULL,
  repair_reason TEXT NOT NULL,
  source_row JSONB NOT NULL,
  archived_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS message_repair_archive_source_idx
  ON message_repair_archive(source_table, source_id);

CREATE TEMP TABLE repair_duplicate_messages ON COMMIT DROP AS
SELECT id
FROM (
  SELECT
    id,
    ROW_NUMBER() OVER (
      PARTITION BY session_id, role, round
      ORDER BY
        CASE WHEN role = 'user' THEN created_at END ASC NULLS LAST,
        CASE WHEN role = 'user' THEN id END ASC NULLS LAST,
        CASE WHEN role = 'patient' THEN created_at END DESC NULLS LAST,
        CASE WHEN role = 'patient' THEN id END DESC NULLS LAST
    ) AS repair_rank
  FROM messages
) ranked
WHERE repair_rank > 1;

INSERT INTO message_repair_archive(source_table, source_id, repair_reason, source_row)
SELECT 'messages', messages.id, 'duplicate_session_role_round', to_jsonb(messages)
FROM messages
JOIN repair_duplicate_messages ON repair_duplicate_messages.id = messages.id;

DELETE FROM messages
USING repair_duplicate_messages
WHERE messages.id = repair_duplicate_messages.id;

CREATE TEMP TABLE repair_duplicate_roleplay_messages ON COMMIT DROP AS
SELECT id
FROM (
  SELECT
    id,
    ROW_NUMBER() OVER (
      PARTITION BY session_id, role, round
      ORDER BY
        CASE WHEN role = 'learner_patient' THEN created_at END ASC NULLS LAST,
        CASE WHEN role = 'learner_patient' THEN id END ASC NULLS LAST,
        CASE WHEN role = 'standard_customer' THEN created_at END DESC NULLS LAST,
        CASE WHEN role = 'standard_customer' THEN id END DESC NULLS LAST
    ) AS repair_rank
  FROM roleplay_messages
) ranked
WHERE repair_rank > 1;

INSERT INTO message_repair_archive(source_table, source_id, repair_reason, source_row)
SELECT 'roleplay_messages', roleplay_messages.id,
       'duplicate_session_role_round', to_jsonb(roleplay_messages)
FROM roleplay_messages
JOIN repair_duplicate_roleplay_messages
  ON repair_duplicate_roleplay_messages.id = roleplay_messages.id;

DELETE FROM roleplay_messages
USING repair_duplicate_roleplay_messages
WHERE roleplay_messages.id = repair_duplicate_roleplay_messages.id;

CREATE UNIQUE INDEX IF NOT EXISTS messages_session_role_round_uidx
  ON messages(session_id, role, round);
CREATE UNIQUE INDEX IF NOT EXISTS roleplay_messages_session_role_round_uidx
  ON roleplay_messages(session_id, role, round);

ALTER TABLE messages
  ADD COLUMN IF NOT EXISTS reply_status TEXT,
  ADD COLUMN IF NOT EXISTS reply_lease_until TIMESTAMPTZ,
  ADD COLUMN IF NOT EXISTS reply_attempt_token TEXT,
  ADD COLUMN IF NOT EXISTS reply_error_type TEXT;

ALTER TABLE roleplay_messages
  ADD COLUMN IF NOT EXISTS reply_status TEXT,
  ADD COLUMN IF NOT EXISTS reply_lease_until TIMESTAMPTZ,
  ADD COLUMN IF NOT EXISTS reply_attempt_token TEXT,
  ADD COLUMN IF NOT EXISTS reply_error_type TEXT;

DO $$
BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_constraint
    WHERE conname = 'messages_reply_status_check' AND conrelid = 'messages'::regclass
  ) THEN
    ALTER TABLE messages ADD CONSTRAINT messages_reply_status_check
      CHECK (reply_status IS NULL OR reply_status IN ('generating', 'ready', 'failed'));
  END IF;
  IF NOT EXISTS (
    SELECT 1 FROM pg_constraint
    WHERE conname = 'roleplay_messages_reply_status_check'
      AND conrelid = 'roleplay_messages'::regclass
  ) THEN
    ALTER TABLE roleplay_messages ADD CONSTRAINT roleplay_messages_reply_status_check
      CHECK (reply_status IS NULL OR reply_status IN ('generating', 'ready', 'failed'));
  END IF;
END $$;

-- Existing input IDs remain reusable.  Complete historical rounds are ready;
-- an input without its model response becomes failed and can be reclaimed.
UPDATE messages AS input
SET reply_status = CASE WHEN EXISTS (
      SELECT 1 FROM messages AS reply
      WHERE reply.session_id = input.session_id
        AND reply.role = 'patient'
        AND reply.round = input.round
    ) THEN 'ready' ELSE 'failed' END,
    reply_lease_until = NULL,
    reply_attempt_token = NULL
WHERE input.role = 'user' AND input.reply_status IS NULL;

UPDATE roleplay_messages AS input
SET reply_status = CASE WHEN EXISTS (
      SELECT 1 FROM roleplay_messages AS reply
      WHERE reply.session_id = input.session_id
        AND reply.role = 'standard_customer'
        AND reply.round = input.round
    ) THEN 'ready' ELSE 'failed' END,
    reply_lease_until = NULL,
    reply_attempt_token = NULL
WHERE input.role = 'learner_patient' AND input.reply_status IS NULL;

CREATE TABLE IF NOT EXISTS ai_jobs (
  id TEXT PRIMARY KEY,
  job_type TEXT NOT NULL CHECK (job_type IN ('evaluation', 'roleplay_summary')),
  target_id TEXT NOT NULL,
  dedupe_key TEXT NOT NULL UNIQUE,
  status TEXT NOT NULL DEFAULT 'pending'
    CHECK (status IN ('pending', 'running', 'retry_wait', 'succeeded', 'dead')),
  generation SMALLINT NOT NULL DEFAULT 1 CHECK (generation BETWEEN 1 AND 100),
  attempts SMALLINT NOT NULL DEFAULT 0 CHECK (attempts BETWEEN 0 AND 100),
  max_attempts SMALLINT NOT NULL DEFAULT 3 CHECK (max_attempts BETWEEN 1 AND 10),
  available_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  lease_until TIMESTAMPTZ,
  worker_id TEXT,
  last_error TEXT,
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  finished_at TIMESTAMPTZ
);

CREATE INDEX IF NOT EXISTS ai_jobs_claim_idx
  ON ai_jobs(status, available_at, created_at);
CREATE INDEX IF NOT EXISTS ai_jobs_expired_lease_idx
  ON ai_jobs(lease_until) WHERE status = 'running';

CREATE TABLE IF NOT EXISTS ai_job_attempts (
  id BIGSERIAL PRIMARY KEY,
  job_id TEXT NOT NULL REFERENCES ai_jobs(id) ON DELETE CASCADE,
  generation SMALLINT NOT NULL DEFAULT 1,
  attempt_number SMALLINT NOT NULL,
  status TEXT NOT NULL CHECK (status IN ('running', 'succeeded', 'retry_wait', 'failed')),
  worker_id TEXT,
  error_type TEXT,
  error_message TEXT,
  started_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  finished_at TIMESTAMPTZ
);

ALTER TABLE ai_jobs
  ADD COLUMN IF NOT EXISTS generation SMALLINT NOT NULL DEFAULT 1;
ALTER TABLE ai_job_attempts
  ADD COLUMN IF NOT EXISTS generation SMALLINT NOT NULL DEFAULT 1;
ALTER TABLE ai_job_attempts
  DROP CONSTRAINT IF EXISTS ai_job_attempts_job_id_attempt_number_key;
CREATE UNIQUE INDEX IF NOT EXISTS ai_job_attempts_generation_uidx
  ON ai_job_attempts(job_id, generation, attempt_number);

-- Backfill work for records already marked generating.  ON CONFLICT makes
-- this migration safely repeatable and the migration itself never calls AI.
INSERT INTO ai_jobs(id, job_type, target_id, dedupe_key, status)
SELECT 'job_eval_' || md5(evaluations.session_id), 'evaluation', evaluations.session_id,
       'evaluation:' || evaluations.session_id, 'pending'
FROM evaluations
WHERE evaluations.status = 'generating'
ON CONFLICT DO NOTHING;

INSERT INTO ai_jobs(id, job_type, target_id, dedupe_key, status)
SELECT 'job_summary_' || md5(roleplay_summaries.session_id), 'roleplay_summary',
       roleplay_summaries.session_id, 'roleplay-summary:' || roleplay_summaries.session_id,
       'pending'
FROM roleplay_summaries
WHERE roleplay_summaries.status = 'generating'
ON CONFLICT DO NOTHING;

COMMIT;
