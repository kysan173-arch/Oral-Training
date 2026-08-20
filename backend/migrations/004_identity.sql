BEGIN;

-- This release intentionally models one institution.  Tenant tables and
-- cross-institution administration are out of scope.
CREATE TABLE IF NOT EXISTS users (
  id TEXT PRIMARY KEY,
  wechat_openid TEXT UNIQUE,
  display_name TEXT NOT NULL DEFAULT '',
  role TEXT NOT NULL DEFAULT 'learner' CHECK (role IN ('learner', 'admin')),
  status TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'disabled')),
  is_demo BOOLEAN NOT NULL DEFAULT FALSE,
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

INSERT INTO users(id, display_name, role, is_demo)
VALUES ('demo-user-001', '演示用户', 'learner', TRUE)
ON CONFLICT (id) DO NOTHING;

-- Preserve any non-default historical user IDs created by earlier local
-- builds before adding foreign keys.
INSERT INTO users(id, display_name, role, is_demo)
SELECT historical.user_id, '历史导入用户', 'learner', historical.user_id = 'demo-user-001'
FROM (
  SELECT DISTINCT user_id FROM sessions
  UNION
  SELECT DISTINCT user_id FROM roleplay_sessions
) AS historical
WHERE historical.user_id IS NOT NULL AND historical.user_id <> ''
ON CONFLICT (id) DO NOTHING;

CREATE TABLE IF NOT EXISTS auth_sessions (
  token_hash TEXT PRIMARY KEY,
  user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  expires_at TIMESTAMPTZ NOT NULL,
  last_seen_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS auth_sessions_expiry_idx ON auth_sessions(expires_at);
CREATE INDEX IF NOT EXISTS users_role_idx ON users(role) WHERE status = 'active';

DO $$
BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_constraint
    WHERE conname = 'sessions_user_id_fkey' AND conrelid = 'sessions'::regclass
  ) THEN
    ALTER TABLE sessions ADD CONSTRAINT sessions_user_id_fkey
      FOREIGN KEY (user_id) REFERENCES users(id);
  END IF;
  IF NOT EXISTS (
    SELECT 1 FROM pg_constraint
    WHERE conname = 'roleplay_sessions_user_id_fkey'
      AND conrelid = 'roleplay_sessions'::regclass
  ) THEN
    ALTER TABLE roleplay_sessions ADD CONSTRAINT roleplay_sessions_user_id_fkey
      FOREIGN KEY (user_id) REFERENCES users(id);
  END IF;
END $$;

COMMIT;
