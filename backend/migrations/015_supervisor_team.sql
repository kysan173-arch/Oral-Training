BEGIN;

-- Supervisor team membership.  A learner belongs to at most one supervisor,
-- which is enforced by making learner_id the primary key instead of adding a
-- surrogate id plus a unique constraint.
DO $$
DECLARE
  first_install BOOLEAN;
BEGIN
  first_install := to_regclass('supervisor_team_members') IS NULL;

  CREATE TABLE IF NOT EXISTS supervisor_team_members (
    learner_id TEXT PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
    supervisor_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CONSTRAINT supervisor_team_members_distinct CHECK (learner_id <> supervisor_id)
  );

  CREATE INDEX IF NOT EXISTS supervisor_team_members_supervisor_idx
    ON supervisor_team_members(supervisor_id, created_at DESC);

  -- Upgrade helper: when exactly one active supervisor exists, every active
  -- learner is placed in that supervisor's team so supervisor dashboards and
  -- member lists do not go blank right after this release.  With more than one
  -- supervisor the assignment is ambiguous, so the teams start empty.
  --
  -- first_install guards the backfill on purpose: without it a migration rerun
  -- would re-add learners a supervisor had deliberately removed.  ON CONFLICT
  -- only protects against duplicate primary keys, not against re-insertion
  -- after a delete.
  IF first_install
     AND (SELECT COUNT(*) FROM users WHERE role = 'admin' AND status = 'active') = 1 THEN
    INSERT INTO supervisor_team_members(learner_id, supervisor_id)
    SELECT u.id, a.id
    FROM users u
    CROSS JOIN (
      SELECT id FROM users WHERE role = 'admin' AND status = 'active' ORDER BY id LIMIT 1
    ) a
    WHERE u.role = 'learner' AND u.status = 'active'
    ON CONFLICT (learner_id) DO NOTHING;
  END IF;
END $$;

COMMIT;
