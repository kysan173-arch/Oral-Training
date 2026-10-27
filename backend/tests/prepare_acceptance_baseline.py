#!/usr/bin/env python3
"""Back up one explicitly named local demo DB and verify restoration in a disposable cluster.
Never seeds, repairs, archives, retries jobs or writes to the source database.
"""
import argparse
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from zoneinfo import ZoneInfo

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'backend'))
from linux_runtime import load_env
from seed_local_test_data import Database


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf8')
    path.chmod(0o600)


def fingerprints(db):
    output = {}
    tables = db.rows("SELECT tablename FROM pg_tables WHERE schemaname='public' ORDER BY tablename")
    for row in tables:
        table = row['tablename']
        if not re.fullmatch(r'[a-z_][a-z_0-9]*', table):
            raise RuntimeError('Unexpected table identifier')
        output[table] = db.rows(f'''SELECT count(*) AS rows,
          md5(COALESCE(string_agg(row_hash, '' ORDER BY row_hash), '')) AS digest
          FROM (SELECT md5(to_jsonb(t)::text) AS row_hash FROM "{table}" t) h''')[0]
    return output


def inventory(db):
    return {
        'database': db.rows('SELECT current_database() AS name, current_setting(\'transaction_read_only\') AS readonly')[0],
        'migrations': db.rows('SELECT name FROM schema_migrations ORDER BY name'),
        'services': db.rows('''SELECT s.id,s.name,s.status,r.version,r.origin,r.content_hash
          FROM clinic_services s LEFT JOIN service_revisions r ON r.id=s.current_revision_id ORDER BY s.name'''),
        'knowledge': db.rows('''SELECT e.id,e.topic,e.scope,e.service_id,e.status,r.title,r.version,r.content_hash
          FROM knowledge_entries e LEFT JOIN knowledge_revisions r ON r.id=e.current_revision_id ORDER BY e.id'''),
        'accounts': db.rows("SELECT role,status,count(*) FROM users GROUP BY role,status ORDER BY role,status"),
        'jobs': db.rows("SELECT job_type,status,count(*) FROM ai_jobs GROUP BY job_type,status ORDER BY job_type,status"),
        'failedJobs': db.rows("SELECT id,job_type,target_id,generation,attempts,last_error FROM ai_jobs WHERE status='dead' ORDER BY id"),
        'scenarios': db.rows('SELECT id,name,is_active,is_template FROM scenarios ORDER BY sort_order'),
        'exactDuplicates': db.rows('''SELECT e.scope,e.service_id,r.title,count(*) FROM knowledge_entries e
          JOIN knowledge_revisions r ON r.id=e.current_revision_id WHERE e.status='active'
          GROUP BY e.scope,e.service_id,r.title,r.body HAVING count(*)>1''')
    }


def catalog(db):
    # Export only synthetic/demo catalog content, never conversations or model settings.
    return {
        'services': db.rows('''SELECT s.id,s.name,s.status,r.id AS revision_id,r.version,r.payload
          FROM clinic_services s JOIN service_revisions r ON r.id=s.current_revision_id
          WHERE r.origin='synthetic' ORDER BY s.id'''),
        'knowledge': db.rows('''SELECT e.id,e.topic,e.scope,e.service_id,e.status,r.id AS revision_id,
          r.version,r.title,r.body,r.metadata FROM knowledge_entries e
          JOIN knowledge_revisions r ON r.id=e.current_revision_id
          WHERE r.metadata->>'origin'='synthetic' AND r.metadata->>'trainingScope'='demo' ORDER BY e.id''')
    }


def verify_restore(args, backup, expected):
    if os.name == 'nt':
        raise RuntimeError('This restore verifier uses Unix sockets; run on the local Linux backend host')
    env = {k: v for k, v in os.environ.items() if not k.startswith('PG') and k != 'DATABASE_URL'}
    with tempfile.TemporaryDirectory(prefix='oral-acceptance-restore-') as temp:
        work = Path(temp)
        socket = work / 'socket'
        socket.mkdir()
        data = work / 'pgdata'
        started = False
        with (args.output / 'restore.log').open('w') as log:
            os.chmod(log.name, 0o600)
            def run(name, *values):
                subprocess.run([str(args.pg_bin / name), *map(str, values)], env=env,
                               stdout=log, stderr=subprocess.STDOUT, check=True, timeout=120)
            try:
                run('initdb', '-D', data, '-A', 'trust', '-U', 'postgres', '--no-locale', '-E', 'UTF8')
                run('pg_ctl', '-D', data, '-l', work / 'postgres.log', '-o',
                    f"-F -c listen_addresses='' -k {socket}", '-w', 'start')
                started = True
                run('createdb', '-h', socket, '-U', 'postgres', 'acceptance_restore')
                run('pg_restore', '--exit-on-error', '--no-owner', '--no-acl', '-h', socket,
                    '-U', 'postgres', '-d', 'acceptance_restore', backup)
                restored = object.__new__(Database)
                restored.env = dict(env, PGHOST=str(socket), PGDATABASE='acceptance_restore',
                                    PGUSER='postgres', PGCONNECT_TIMEOUT='5')
                actual = fingerprints(restored)
                if actual != expected:
                    changed = sorted(k for k in set(actual) | set(expected) if actual.get(k) != expected.get(k))
                    raise RuntimeError('Restore fingerprint mismatch: ' + ', '.join(changed))
                return {'status': 'passed', 'tables': len(actual), 'verification': 'row counts and full-row multiset digests'}
            finally:
                if started:
                    run('pg_ctl', '-D', data, '-m', 'fast', '-w', 'stop')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--database', required=True)
    parser.add_argument('--env-file', type=Path, default=ROOT / 'backend/backend.env')
    parser.add_argument('--pg-bin', type=Path, default=Path('/usr/lib/postgresql/16/bin'))
    parser.add_argument('--output', type=Path, required=True, help='New private directory outside the repository')
    args = parser.parse_args()
    args.output = args.output.expanduser().resolve()
    if args.output == ROOT or ROOT in args.output.parents:
        parser.error('Backups must stay outside the repository')
    env = load_env(args.env_file)
    if env.get('PRODUCTION', '').lower() not in ('false', '0', 'no', 'off') or env.get('AUTH_MODE') != 'demo':
        parser.error('Only explicit non-production demo configuration is allowed')
    db = Database(env, args.database)  # loopback, exact DB name, no URI overrides
    args.output.mkdir(mode=0o700, parents=True, exist_ok=False)
    before = fingerprints(db)
    write_json(args.output / 'inventory.json', inventory(db))
    write_json(args.output / 'catalog.json', catalog(db))
    write_json(args.output / 'source-fingerprints.json', before)
    backup = args.output / 'database.dump'
    backup.touch(mode=0o600, exist_ok=False)
    with (args.output / 'backup.log').open('w') as log:
        os.chmod(log.name, 0o600)
        subprocess.run([str(args.pg_bin / 'pg_dump'), '-Fc', '--no-owner', '--no-acl', '-f', str(backup)],
                       env=dict(db.env, PGOPTIONS='-c default_transaction_read_only=on'),
                       stdout=log, stderr=subprocess.STDOUT, check=True, timeout=180)
    if fingerprints(db) != before:
        raise RuntimeError('Source changed during backup; keep this snapshot but rerun before editing')
    restored = verify_restore(args, backup, before)
    result = {'database': args.database, 'createdAt': datetime.now(ZoneInfo('Asia/Shanghai')).isoformat(),
              'backupSha256': hashlib.sha256(backup.read_bytes()).hexdigest(), 'restore': restored,
              'sourceUnchanged': fingerprints(db) == before, 'writesToSource': 0}
    write_json(args.output / 'verification.json', result)
    if not result['sourceUnchanged']:
        raise RuntimeError('Source changed during verification; rebaseline before editing')
    print(json.dumps({'status': 'passed', 'output': str(args.output), 'restoreTables': restored['tables'],
                      'writesToSource': 0}, ensure_ascii=False))


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        # Subprocess details may contain connection diagnostics; inspect private logs locally.
        print('Baseline failed: ' + (str(error) if isinstance(error, RuntimeError) else type(error).__name__), file=sys.stderr)
        sys.exit(1)
