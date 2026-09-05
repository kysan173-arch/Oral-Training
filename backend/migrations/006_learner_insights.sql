BEGIN;

-- Report-derived phrases and mistakes remain in evaluations.report so their
-- source remains auditable.  This table stores only the learner's reversible
-- mastery state for a specific report item.
CREATE TABLE IF NOT EXISTS learner_mistake_progress (
  user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  session_id TEXT NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,
  mistake_key TEXT NOT NULL CHECK (char_length(mistake_key) BETWEEN 1 AND 120),
  mastered_at TIMESTAMPTZ,
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  PRIMARY KEY (user_id, session_id, mistake_key)
);

CREATE INDEX IF NOT EXISTS learner_mistake_progress_user_idx
  ON learner_mistake_progress(user_id, updated_at DESC);

COMMIT;
