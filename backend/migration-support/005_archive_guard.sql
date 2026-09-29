-- Compatibility preflight for the immutable 005 migration. Run immediately
-- before 005, with the backend stopped and the same search_path. The trigger
-- captures the complete source BEFORE 005 resets it, including SQL NULL summary.
CREATE TABLE IF NOT EXISTS generation_state_repair_archive (
  id BIGSERIAL PRIMARY KEY,
  source_table TEXT NOT NULL CHECK (source_table IN ('evaluations', 'roleplay_summaries', 'ai_jobs')),
  source_id TEXT NOT NULL,
  repair_reason TEXT NOT NULL,
  source_row JSONB NOT NULL,
  archived_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  UNIQUE(source_table, source_id, repair_reason)
);

CREATE OR REPLACE FUNCTION capture_v005_summary_row() RETURNS trigger
LANGUAGE plpgsql AS $$
BEGIN
  IF NEW.source_table = 'roleplay_summaries'
     AND NEW.repair_reason = 'regenerate_inconsistent_state_v005' THEN
    SELECT to_jsonb(original_row.*) INTO STRICT NEW.source_row
    FROM roleplay_summaries AS original_row WHERE session_id = NEW.source_id;
  END IF;
  RETURN NEW;
END;
$$;

DROP TRIGGER IF EXISTS capture_v005_summary_row ON generation_state_repair_archive;
CREATE TRIGGER capture_v005_summary_row BEFORE INSERT ON generation_state_repair_archive
FOR EACH ROW EXECUTE FUNCTION capture_v005_summary_row();
