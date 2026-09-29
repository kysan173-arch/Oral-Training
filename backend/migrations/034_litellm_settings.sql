BEGIN;

-- One institution, one gateway. API keys are Windows DPAPI ciphertext, never plaintext.
CREATE TABLE IF NOT EXISTS model_gateway_settings (
  singleton BOOLEAN PRIMARY KEY DEFAULT TRUE CHECK (singleton),
  base_url TEXT NOT NULL DEFAULT '',
  model TEXT NOT NULL DEFAULT '',
  api_key_encrypted TEXT NOT NULL DEFAULT '',
  revision BIGINT NOT NULL DEFAULT 0,
  updated_by TEXT NOT NULL DEFAULT '',
  updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
INSERT INTO model_gateway_settings(singleton) VALUES(TRUE) ON CONFLICT DO NOTHING;

CREATE TABLE IF NOT EXISTS model_gateway_settings_audit (
  revision BIGINT PRIMARY KEY,
  base_url TEXT NOT NULL,
  model TEXT NOT NULL,
  action TEXT NOT NULL CHECK (action IN ('save', 'clear')),
  updated_by TEXT NOT NULL,
  updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

COMMIT;
