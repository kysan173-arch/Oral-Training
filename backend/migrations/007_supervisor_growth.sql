BEGIN;

-- Points have exactly one source in this release: one daily check-in in the
-- Asia/Shanghai business day.  There is intentionally no balance table,
-- exchange ledger, leaderboard, or other earning source.
CREATE TABLE IF NOT EXISTS learner_checkins (
  user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  checkin_date DATE NOT NULL,
  points SMALLINT NOT NULL DEFAULT 10 CHECK (points = 10),
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  PRIMARY KEY (user_id, checkin_date)
);

CREATE INDEX IF NOT EXISTS learner_checkins_user_date_idx
  ON learner_checkins(user_id, checkin_date DESC);

-- Favorites reference report-derived phrase keys.  The API validates that a
-- key belongs to the requesting learner's completed report before writing.
CREATE TABLE IF NOT EXISTS learner_phrase_favorites (
  user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  session_id TEXT NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,
  phrase_key TEXT NOT NULL CHECK (char_length(phrase_key) BETWEEN 1 AND 120),
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  PRIMARY KEY (user_id, session_id, phrase_key)
);

CREATE INDEX IF NOT EXISTS learner_phrase_favorites_user_idx
  ON learner_phrase_favorites(user_id, updated_at DESC);

COMMIT;
