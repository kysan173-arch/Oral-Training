BEGIN;

-- Hints move from "three per session" to "three per session, at most one per
-- round": the same total budget, but the learner must spend it across distinct
-- turns instead of burning all three on the opening question.
--
-- The uniqueness key therefore changes from hint_number to round, because the
-- database has to reject a second hint inside the same round on its own -- not
-- just rely on the API counting rows before inserting.  hint_number is kept so
-- legacy rows stay auditable and ordering stays stable.

ALTER TABLE session_hints
  ADD COLUMN IF NOT EXISTS round INTEGER;

-- Clean up the legacy "hint_number between 1 and 3" ceiling, which would
-- otherwise block the (session_id, round) uniqueness key from serving rounds 4+.
ALTER TABLE session_hints
  DROP CONSTRAINT IF EXISTS session_hints_hint_number_check;

-- Backfill: hints recorded before this migration were written against whatever
-- round was current at the time, and the old schema did not record it.  Two
-- sources are used, in priority order:
--
--   1. The round of the patient message that was showing when the hint was
--      written.  Because old rows carry no timestamp-to-round binding, this is
--      read as "the highest patient round not after hint_number", which is the
--      same ordering the messages were inserted in.
--   2. Legacy rows with no round information at all: default them to the
--      session's current round, matching the only semantics the old code had.
--
-- Historical sessions routinely spent all three hints while the patient had
-- only spoken at round 0 (the learner clicked the button three times before
-- answering), so step 1 collapses several rows onto one round.  The uniqueness
-- key below would then fail outright: real rows in this database do exactly
-- that.
--
-- So the rows are walked per session in hint_number order and each round is
-- raised to the next free number (round = max(proposed, previous round + 1)).
-- A plain loop beats a window expression here: the rule is inherently recursive,
-- and this is a one-off backfill over a handful of rows per session, so clarity
-- is worth more than a clever single statement.  The exact per-round binding of
-- pre-existing data cannot be recovered; keeping every hint intact matters more
-- than a fabricated attribution.
DO $$
DECLARE
  row RECORD;
  session_key TEXT := NULL;
  previous_round INTEGER := NULL;
BEGIN
  FOR row IN
    SELECT h.id, h.session_id, h.hint_number,
           COALESCE((
             SELECT MAX(m.round) FROM messages AS m
             WHERE m.session_id = h.session_id AND m.role = 'patient'
               AND m.round <= h.hint_number
           ), s.current_round) AS proposed
    FROM session_hints AS h
    JOIN sessions AS s ON s.id = h.session_id
    WHERE h.round IS NULL
    ORDER BY h.session_id, h.hint_number
  LOOP
    IF session_key IS DISTINCT FROM row.session_id THEN
      session_key := row.session_id;
      previous_round := NULL;
    END IF;
    IF previous_round IS NULL OR row.proposed > previous_round + 1 THEN
      previous_round := row.proposed;
    ELSE
      previous_round := previous_round + 1;
    END IF;
    UPDATE session_hints SET round = previous_round WHERE id = row.id;
  END LOOP;
END $$;

-- Last resort for rows whose session row is gone: fall back to the stored
-- hint_number so round can be made NOT NULL.
UPDATE session_hints SET round = hint_number WHERE round IS NULL;

ALTER TABLE session_hints ALTER COLUMN round SET NOT NULL;

COMMENT ON COLUMN session_hints.round IS
  'Training round this hint answers. One hint per round; hint_number stays as the global ordinal (1..3).';

-- Replace the old per-session ordinal key with the per-round key.  DROP-then-ADD
-- keeps the script rerunnable even though a first run already applied the change.
DO $$
BEGIN
  IF EXISTS (
    SELECT 1 FROM pg_constraint
    WHERE conname = 'session_hints_session_id_hint_number_key'
      AND conrelid = 'session_hints'::regclass
  ) THEN
    ALTER TABLE session_hints DROP CONSTRAINT session_hints_session_id_hint_number_key;
  END IF;

  IF NOT EXISTS (
    SELECT 1 FROM pg_constraint
    WHERE conname = 'session_hints_session_id_round_key'
      AND conrelid = 'session_hints'::regclass
  ) THEN
    ALTER TABLE session_hints
      ADD CONSTRAINT session_hints_session_id_round_key UNIQUE (session_id, round);
  END IF;
END $$;

DROP INDEX IF EXISTS session_hints_session_idx;
CREATE INDEX IF NOT EXISTS session_hints_session_idx
  ON session_hints(session_id, hint_number);

COMMIT;
