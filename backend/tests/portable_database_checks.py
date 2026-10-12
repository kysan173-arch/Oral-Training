"""Run DB regressions in independent schemas inside the harness-owned cluster."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]


def check_databases(run, env, pg_bin, bin_dir, db):
    psql = [pg_bin / 'psql', db, '-X', '-At', '-v', 'ON_ERROR_STOP=1']
    assert run(psql + ['-c', 'SELECT current_database()']) == 'litellm_config_test'
    check_history(run, env, pg_bin, db)
    migrations = sorted((ROOT / 'backend/migrations').glob('[0-9][0-9][0-9]_*.sql'))
    for binary, schema in [('database_feature_test', 'feature_test'),
                           ('scenario_draft_test', 'scenario_test'),
                           ('knowledge_store_database_test', 'knowledge_test'),
                           ('audit_regression_test', 'audit_fix_portable'),
                           ('patient_initialization_test', 'patient_init_portable'),
                           ('embedding_rag_test', 'embedding_test')]:
        target_db = db
        if binary == 'database_feature_test':
            # This test deliberately overrides connection options for lock probes;
            # use a whole isolated database so that search_path cannot be lost.
            run(psql + ['-c', 'CREATE DATABASE portable_feature_test'])
            target_db = db.rsplit('/', 1)[0] + '/portable_feature_test'
            schema = 'public'
        psql = [pg_bin / 'psql', target_db, '-X', '-At', '-v', 'ON_ERROR_STOP=1']
        if schema != 'public':
            run(psql + ['-c', f'CREATE SCHEMA {schema}'])
        env['PGOPTIONS'] = f'-c search_path={schema}'
        env['ORAL_TRAINING_TEST_DATABASE_URL'] = target_db
        env['ORAL_EMBEDDING_TEST_ISOLATED'] = '1'
        if binary == 'patient_initialization_test':
            for migration in migrations:
                if migration.name.startswith('005_'):
                    run(psql + ['-f', ROOT / 'backend/migration-support/005_archive_guard.sql'])
                if migration.name.startswith('032_'):
                    run(psql + ['-c', """
INSERT INTO sessions(id,user_id,scenario_id,scenario_name,status,current_round,max_rounds,patient_state)
VALUES ('init-migration-active','demo-user-001','implant-basic','Legacy active','in_progress',0,10,'{}'),
       ('init-migration-done','demo-user-001','price-comparison','Legacy done','completed',1,10,'{}');
INSERT INTO evaluations(session_id,status,report) VALUES ('init-migration-done','generating',NULL);
INSERT INTO ai_jobs(id,job_type,target_id,dedupe_key,status,available_at)
VALUES ('init-migration-job','evaluation','init-migration-done','evaluation:init-migration-done','pending',NOW()+INTERVAL '1 day');
"""])
                run(psql + ['-f', migration])
            run(psql + ['-f', ROOT / 'backend/migrations/032_patient_initialization_jobs.sql'])
        else:
            migrate = [sys.executable, ROOT / 'backend/migrate.py', '--database-url', target_db,
                       '--psql', pg_bin / 'psql', '--schema', schema]
            run(migrate)
            run(migrate)
        print(run([bin_dir / binary]), flush=True)
        digest = 'SELECT md5(COALESCE(jsonb_agg(to_jsonb(s) ORDER BY id)::text,\'\')) FROM sessions s'
        before = run(psql + ['-c', digest])
        if binary == 'patient_initialization_test':
            run(psql + ['-f', ROOT / 'backend/migrations/032_patient_initialization_jobs.sql'])
        else:
            run(migrate)
        assert before == run(psql + ['-c', digest]), 'Migration rerun rewrote sessions'
        env.pop('PGOPTIONS')
        env.pop('ORAL_TRAINING_TEST_DATABASE_URL')
        psql = [pg_bin / 'psql', db, '-X', '-At', '-v', 'ON_ERROR_STOP=1']
    env.pop('ORAL_EMBEDDING_TEST_ISOLATED')


def check_history(run, env, pg_bin, db):
    schema = 'migration_history_test'
    psql = [pg_bin / 'psql', db, '-X', '-At', '-v', 'ON_ERROR_STOP=1']
    run(psql + ['-c', f'CREATE SCHEMA {schema}'])
    env['PGOPTIONS'] = f'-c search_path={schema}'
    for name in ['001_initial.sql', '002_roleplay.sql']:
        run(psql + ['-f', ROOT / 'backend/migrations' / name])
    run(psql + ['-f', ROOT / 'backend/tests/fixtures/reliability_history.sql'])
    # Capture complete original rows before migrations perform any repair.
    original = run(psql + ['-c', "SELECT jsonb_agg(to_jsonb(m)) FROM messages m"])
    migrate = [sys.executable, ROOT / 'backend/migrate.py', '--database-url', db,
               '--psql', pg_bin / 'psql', '--schema', schema]
    def rejected(command, expected):
        try:
            run(command)
        except RuntimeError as error:
            assert expected in str(error), error
        else:
            raise AssertionError('Migration should have failed')
    rejected(migrate, 'requires --baseline-through')
    run(migrate + ['--baseline-through', '2'])
    import json
    for row in json.loads(original):
        current = run(psql + ['-c', f"SELECT content FROM messages WHERE id='{row['id']}'"])
        if current != row['content']:
            archive = run(psql + ['-c', f"SELECT source_row FROM message_repair_archive WHERE source_id='{row['id']}' ORDER BY id LIMIT 1"])
            archived = json.loads(archive)
            assert all(archived.get(name) == value for name, value in row.items()), ('Repair archive lost original fields', row['id'])
    assert run(psql + ['-c', "SELECT count(*) FROM message_pair_repair_audit WHERE repair_status='resolved'"]) == '2'
    assert run(psql + ['-c', "SELECT count(*) FROM ai_jobs WHERE status='pending'"]) != '0'
    snapshot = "SELECT md5(jsonb_agg(to_jsonb(j) ORDER BY id)::text) FROM ai_jobs j"
    before = run(psql + ['-c', snapshot])
    run(migrate)
    assert before == run(psql + ['-c', snapshot]), 'Reapplying migrations changed pending jobs'
    checksum = run(psql + ['-c', "SELECT sha256 FROM schema_migrations WHERE name='001_initial.sql'"])
    run(psql + ['-c', "UPDATE schema_migrations SET sha256='altered' WHERE name='001_initial.sql'"])
    rejected(migrate, 'Shipped migration checksum changed')
    run(psql + ['-c', f"UPDATE schema_migrations SET sha256='{checksum}' WHERE name='001_initial.sql'"])
    env.pop('PGOPTIONS')
    print('PASS: existing DB baseline refusal, historical duplicate repair/full archives, pending jobs, ledger rerun/checksum rejection')
