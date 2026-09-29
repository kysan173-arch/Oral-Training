BEGIN;
ALTER TABLE sessions
  ADD COLUMN IF NOT EXISTS service_id TEXT REFERENCES clinic_services(id) ON DELETE RESTRICT,
  ADD COLUMN IF NOT EXISTS service_revision_id TEXT REFERENCES service_revisions(id) ON DELETE RESTRICT,
  ADD COLUMN IF NOT EXISTS client_session_id TEXT,
  ADD COLUMN IF NOT EXISTS context_version INTEGER NOT NULL DEFAULT 1;

DROP INDEX IF EXISTS one_active_session_per_scenario;
CREATE UNIQUE INDEX IF NOT EXISTS one_active_legacy_training_session
  ON sessions(user_id, scenario_id) WHERE status = 'in_progress' AND service_id IS NULL;
CREATE UNIQUE INDEX IF NOT EXISTS one_active_service_training_session
  ON sessions(user_id, scenario_id, service_id) WHERE status = 'in_progress' AND service_id IS NOT NULL;
CREATE UNIQUE INDEX IF NOT EXISTS training_client_session_id_idx
  ON sessions(user_id, client_session_id) WHERE client_session_id IS NOT NULL;

ALTER TABLE training_contexts
  ADD COLUMN IF NOT EXISTS initialization_status TEXT NOT NULL DEFAULT 'pending'
    CHECK (initialization_status IN ('pending', 'generating', 'ready', 'failed')),
  ADD COLUMN IF NOT EXISTS initialization_generation INTEGER NOT NULL DEFAULT 1
    CHECK (initialization_generation BETWEEN 1 AND 100),
  ADD COLUMN IF NOT EXISTS initialization_error TEXT,
  ADD COLUMN IF NOT EXISTS private_profile JSONB CHECK (jsonb_typeof(private_profile) = 'object'),
  ADD COLUMN IF NOT EXISTS public_profile JSONB CHECK (jsonb_typeof(public_profile) = 'object'),
  ADD COLUMN IF NOT EXISTS patient_state JSONB CHECK (jsonb_typeof(patient_state) = 'object'),
  ADD COLUMN IF NOT EXISTS initialized_at TIMESTAMPTZ,
  ADD COLUMN IF NOT EXISTS initialization_model_version TEXT;
ALTER TABLE ai_jobs DROP CONSTRAINT IF EXISTS ai_jobs_job_type_check;
ALTER TABLE ai_jobs ADD CONSTRAINT ai_jobs_job_type_check
  CHECK (job_type IN ('evaluation', 'roleplay_summary', 'patient_initialization'));
CREATE INDEX IF NOT EXISTS patient_initialization_target_idx
  ON ai_jobs(target_id, generation) WHERE job_type = 'patient_initialization';
COMMIT;
