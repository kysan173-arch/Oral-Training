BEGIN;

CREATE TABLE IF NOT EXISTS user_model_gateway_settings (
  user_id TEXT PRIMARY KEY REFERENCES users(id),
  base_url TEXT NOT NULL DEFAULT '',
  model TEXT NOT NULL DEFAULT '',
  api_key_encrypted TEXT NOT NULL DEFAULT '',
  revision BIGINT NOT NULL DEFAULT 0,
  updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
CREATE TABLE IF NOT EXISTS user_model_gateway_settings_audit (
  user_id TEXT NOT NULL REFERENCES users(id),
  revision BIGINT NOT NULL,
  base_url TEXT NOT NULL,
  model TEXT NOT NULL,
  action TEXT NOT NULL CHECK (action IN ('save', 'clear', 'migrate')),
  updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  PRIMARY KEY (user_id, revision)
);

-- Preserve the complete former shared configuration and audit in the old tables.
-- Only its last author receives a personal copy; never distribute a shared key
-- to every learner. Existing personal settings (including cleared ones) win.
WITH migrated AS (
  INSERT INTO user_model_gateway_settings(user_id,base_url,model,api_key_encrypted,revision,updated_at)
  SELECT s.updated_by,s.base_url,s.model,s.api_key_encrypted,s.revision,s.updated_at
  FROM model_gateway_settings s JOIN users u ON u.id=s.updated_by
  WHERE s.api_key_encrypted<>''
  ON CONFLICT DO NOTHING
  RETURNING *
)
INSERT INTO user_model_gateway_settings_audit(user_id,revision,base_url,model,action,updated_at)
SELECT user_id,revision,base_url,model,'migrate',updated_at FROM migrated
ON CONFLICT DO NOTHING;

COMMIT;
