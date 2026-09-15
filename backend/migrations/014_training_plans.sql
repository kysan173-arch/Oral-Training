BEGIN;

-- Training plans published by supervisors, plus per-learner assignment rows.
-- Progress is deliberately NOT accumulated here: it is recomputed on read by
-- joining completed sessions inside the plan window, so late-joining learners
-- are counted automatically and there is no counter drift to reconcile.
CREATE TABLE IF NOT EXISTS training_plans (
  id TEXT PRIMARY KEY,
  title TEXT NOT NULL CHECK (char_length(title) BETWEEN 1 AND 100),
  period TEXT NOT NULL CHECK (period IN ('week', 'month')),
  scenario_ids JSONB NOT NULL DEFAULT '[]'::jsonb,
  required_count SMALLINT NOT NULL DEFAULT 1 CHECK (required_count BETWEEN 1 AND 20),
  required_pass_rate SMALLINT NOT NULL DEFAULT 60
    CHECK (required_pass_rate BETWEEN 0 AND 100),
  description TEXT NOT NULL DEFAULT '',
  due_at TIMESTAMPTZ NOT NULL,
  created_by TEXT NOT NULL REFERENCES users(id),
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- Expiry is derived from due_at (due_at <= NOW()) instead of a status column so
-- the stored state can never disagree with the clock.
CREATE INDEX IF NOT EXISTS training_plans_due_idx ON training_plans(due_at DESC);
CREATE INDEX IF NOT EXISTS training_plans_created_idx ON training_plans(created_at DESC);

CREATE TABLE IF NOT EXISTS training_assignments (
  plan_id TEXT NOT NULL REFERENCES training_plans(id) ON DELETE CASCADE,
  learner_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  assigned_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  is_notified BOOLEAN NOT NULL DEFAULT FALSE,
  PRIMARY KEY (plan_id, learner_id)
);

CREATE INDEX IF NOT EXISTS training_assignments_learner_idx
  ON training_assignments(learner_id, assigned_at DESC);

COMMIT;
