BEGIN;

-- Categories are stored with the existing scenarios so both training modes
-- can present the same real, data-driven grouping without client-side mock data.
ALTER TABLE scenarios ADD COLUMN IF NOT EXISTS category TEXT;

UPDATE scenarios
SET category = CASE id
  WHEN 'price-comparison' THEN 'price_negotiation'
  WHEN 'post-treatment-discomfort' THEN 'complaint_handling'
  ELSE 'consultation'
END
WHERE category IS NULL OR btrim(category) = '';

ALTER TABLE scenarios ALTER COLUMN category SET DEFAULT 'consultation';
ALTER TABLE scenarios ALTER COLUMN category SET NOT NULL;

DO $$
BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_constraint
    WHERE conname = 'scenarios_category_check' AND conrelid = 'scenarios'::regclass
  ) THEN
    ALTER TABLE scenarios ADD CONSTRAINT scenarios_category_check
      CHECK (category IN ('consultation', 'price_negotiation', 'complaint_handling', 'recommendation'));
  END IF;
END $$;

CREATE TABLE IF NOT EXISTS session_hints (
  id TEXT PRIMARY KEY,
  session_id TEXT NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,
  hint_number SMALLINT NOT NULL CHECK (hint_number BETWEEN 1 AND 3),
  content TEXT NOT NULL CHECK (char_length(content) BETWEEN 1 AND 600),
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  UNIQUE(session_id, hint_number)
);

CREATE INDEX IF NOT EXISTS session_hints_session_idx
  ON session_hints(session_id, hint_number);

COMMIT;
