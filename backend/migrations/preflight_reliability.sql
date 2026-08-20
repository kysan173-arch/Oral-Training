-- Read-only checks to run after taking a backup and before the maintenance
-- window.  This file does not lock or modify business rows.
SELECT 'messages_duplicate_round_groups' AS check_name, COUNT(*) AS affected
FROM (
  SELECT session_id, role, round FROM messages
  GROUP BY session_id, role, round HAVING COUNT(*) > 1
) duplicates
UNION ALL
SELECT 'roleplay_duplicate_round_groups', COUNT(*)
FROM (
  SELECT session_id, role, round FROM roleplay_messages
  GROUP BY session_id, role, round HAVING COUNT(*) > 1
) duplicates
UNION ALL
SELECT 'training_inputs_without_reply', COUNT(*)
FROM messages input
WHERE input.role = 'user' AND NOT EXISTS (
  SELECT 1 FROM messages reply
  WHERE reply.session_id = input.session_id AND reply.role = 'patient'
    AND reply.round = input.round
)
UNION ALL
SELECT 'roleplay_inputs_without_reply', COUNT(*)
FROM roleplay_messages input
WHERE input.role = 'learner_patient' AND NOT EXISTS (
  SELECT 1 FROM roleplay_messages reply
  WHERE reply.session_id = input.session_id AND reply.role = 'standard_customer'
    AND reply.round = input.round
)
UNION ALL
SELECT 'generating_evaluations', COUNT(*) FROM evaluations WHERE status = 'generating'
UNION ALL
SELECT 'generating_roleplay_summaries', COUNT(*) FROM roleplay_summaries WHERE status = 'generating'
UNION ALL
SELECT 'max_round_in_progress_sessions', COUNT(*) FROM sessions
WHERE status = 'in_progress' AND current_round >= max_rounds
UNION ALL
SELECT 'max_round_in_progress_roleplay_sessions', COUNT(*) FROM roleplay_sessions
WHERE status = 'in_progress' AND current_round >= max_rounds
ORDER BY check_name;
