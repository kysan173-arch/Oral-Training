BEGIN;

-- Allow learners to override the default patient profile for a training session.
-- The custom profile is stored per-session so it does not affect other learners
-- or the shared scenario template.
ALTER TABLE sessions
  ADD COLUMN IF NOT EXISTS custom_patient_profile JSONB;

COMMENT ON COLUMN sessions.custom_patient_profile IS
  'Learner-defined patient profile (age, description, emotion) that overrides the scenario default for this session only.';

COMMIT;
