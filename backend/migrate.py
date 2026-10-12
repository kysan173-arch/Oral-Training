#!/usr/bin/env python3
"""Ordered PostgreSQL migrations, sharing the ledger/checksums with migrate.ps1."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess
import tempfile
import shlex
from urllib.parse import urlsplit, unquote, parse_qsl

ROOT = Path(__file__).resolve().parent


def migration_script(schema='public', baseline=0):
    if not re.fullmatch(r'[a-zA-Z_][a-zA-Z0-9_]*', schema) or not 0 <= baseline <= 999:
        raise ValueError('Invalid schema or baseline')
    files = sorted(p for p in (ROOT / 'migrations').glob('*.sql') if re.match(r'^\d{3}_', p.name))
    if not files:
        raise ValueError('No numbered migrations found')
    sql = [r'\set ON_ERROR_STOP on', f'SET search_path TO {schema};',
           f"SELECT pg_advisory_lock(hashtext(current_database()), hashtext('{schema}:migrations'));",
           "SELECT to_regclass('schema_migrations') IS NULL AS no_ledger, "
           "to_regclass('sessions') IS NOT NULL AS existing_install \\gset",
           r'\if :no_ledger', r'\if :existing_install']
    if baseline == 0:
        sql.append("DO $$ BEGIN RAISE EXCEPTION 'Existing database requires --baseline-through with "
                   "the verified last applied migration; stop the backend first'; END $$;")
    sql += [r'\endif', r'\endif', 'CREATE TABLE IF NOT EXISTS schema_migrations '
            '(name TEXT PRIMARY KEY, sha256 TEXT NOT NULL, applied_at TIMESTAMPTZ NOT NULL DEFAULT NOW(), '
            'baselined BOOLEAN NOT NULL DEFAULT FALSE);']
    for path in files:
        name, digest = path.name, hashlib.sha256(path.read_bytes()).hexdigest()
        number = int(name[:3])
        if number <= baseline:
            sql.append(f"INSERT INTO schema_migrations(name,sha256,baselined) VALUES "
                       f"('{name}','{digest}',TRUE) ON CONFLICT DO NOTHING;")
        sql += [f"DO $$ BEGIN IF EXISTS (SELECT 1 FROM schema_migrations WHERE name='{name}' "
                f"AND sha256<>'{digest}') THEN RAISE EXCEPTION 'Shipped migration checksum changed: {name}'; "
                "END IF; END $$;",
                f"SELECT NOT EXISTS (SELECT 1 FROM schema_migrations WHERE name='{name}') AS apply_migration \\gset",
                r'\if :apply_migration']
        if number == 5:
            guard = (ROOT / 'migration-support/005_archive_guard.sql').as_posix().replace("'", "''")
            sql.append(f"\\i '{guard}'")
        include = path.as_posix().replace("'", "''")
        sql += [f'\\echo Applying {name}', f"\\i '{include}'",
                f"INSERT INTO schema_migrations(name,sha256) VALUES ('{name}','{digest}');", r'\endif']
    return '\n'.join(sql) + '\n'


def migrate(database_url, psql='psql', schema='public', baseline=0):
    if not database_url:
        raise ValueError('DATABASE_URL or --database-url is required')
    # Keep credentials out of child argv and do not execute startup psqlrc files.
    env = os.environ.copy()
    # PGDATABASE containing a URI is not expanded by psql/libpq. Split the DSN
    # into individual libpq environment variables; never put passwords in argv.
    if database_url.startswith(('postgresql://', 'postgres://')):
        url = urlsplit(database_url)
        values = dict(parse_qsl(url.query))
        values.update({k: unquote(v) for k, v in {
            'host': url.hostname, 'port': str(url.port) if url.port else None,
            'user': url.username, 'password': url.password, 'dbname': url.path[1:]
        }.items() if v is not None and v != ''})
    else:
        values = {}
        for part in shlex.split(database_url):
            name, separator, value = part.partition('=')
            if not separator:
                raise ValueError('Use a PostgreSQL URI or key=value connection string')
            values[name] = value
    names = {'dbname': 'PGDATABASE', 'user': 'PGUSER', 'password': 'PGPASSWORD',
             'host': 'PGHOST', 'hostaddr': 'PGHOSTADDR', 'port': 'PGPORT',
             'sslmode': 'PGSSLMODE', 'sslrootcert': 'PGSSLROOTCERT', 'sslcert': 'PGSSLCERT',
             'sslkey': 'PGSSLKEY', 'connect_timeout': 'PGCONNECT_TIMEOUT',
             'application_name': 'PGAPPNAME', 'options': 'PGOPTIONS', 'service': 'PGSERVICE',
             'passfile': 'PGPASSFILE', 'channel_binding': 'PGCHANNELBINDING',
             'target_session_attrs': 'PGTARGETSESSIONATTRS'}
    for name in ['PGDATABASE', 'PGUSER', 'PGPASSWORD', 'PGHOST', 'PGHOSTADDR', 'PGPORT', 'PGSERVICE']:
        env.pop(name, None)
    for name, value in values.items():
        if name not in names:
            raise ValueError(f'Unsupported PostgreSQL connection option: {name}')
        env[names[name]] = value
    with tempfile.TemporaryDirectory(prefix='oral-migrate-') as temp:
        path = Path(temp) / 'migrate.sql'
        path.write_text(migration_script(schema, baseline), encoding='utf-8')
        subprocess.run([str(psql), '-X', '-v', 'ON_ERROR_STOP=1', '-f', str(path)], env=env, check=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--database-url', default=os.environ.get('DATABASE_URL'))
    parser.add_argument('--psql', default='psql')
    parser.add_argument('--schema', default='public')
    parser.add_argument('--baseline-through', type=int, default=0)
    args = parser.parse_args()
    migrate(args.database_url, args.psql, args.schema, args.baseline_through)
