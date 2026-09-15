BEGIN;

-- Record the patient's emotion at the moment each AI patient reply was generated,
-- so the training chat can show a per-turn emotion label next to that reply.
-- The column is intentionally unconstrained free text: the model replies use the
-- 平静/犹豫/焦虑/缓和 whitelist, but the opening message may carry the learner's
-- hand-typed custom profile emotion, which is not limited to that whitelist.
-- Learner messages and rows created before this migration stay NULL, and the
-- client hides the label for them.
ALTER TABLE messages
  ADD COLUMN IF NOT EXISTS emotion TEXT;

COMMENT ON COLUMN messages.emotion IS
  'Patient emotion snapshot for this AI reply. Free text; NULL for learner messages and legacy rows.';

COMMIT;
