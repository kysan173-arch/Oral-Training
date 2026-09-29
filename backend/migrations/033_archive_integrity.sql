\ir ../migration-support/005_archive_guard.sql

BEGIN;

-- Previously shipped 005 could archive only the summary JSON. Never replace
-- that evidence with a present-day row and pretend it is the original source.
ALTER TABLE generation_state_repair_archive
  ADD COLUMN IF NOT EXISTS source_row_complete BOOLEAN NOT NULL DEFAULT TRUE;
UPDATE generation_state_repair_archive
SET source_row_complete = FALSE
WHERE source_table = 'roleplay_summaries'
  AND repair_reason = 'regenerate_inconsistent_state_v005'
  AND (jsonb_typeof(source_row) = 'object'
    AND source_row->>'session_id' = source_id
    AND source_row ?& ARRAY['status', 'summary', 'model_version', 'prompt_version', 'generated_at', 'updated_at']) IS NOT TRUE;

COMMENT ON COLUMN generation_state_repair_archive.source_row_complete IS
  'False identifies incomplete legacy archives; original evidence is retained, not fabricated.';

COMMIT;
