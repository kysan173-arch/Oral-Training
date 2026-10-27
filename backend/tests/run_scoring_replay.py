#!/usr/bin/env python3
"""Synthetic component replay. Default is offline; live requires explicit batch and budget.

Live starts a new disposable PostgreSQL cluster and UUID-named database.
Existing PGHOST/PGUSER/PGDATABASE settings are ignored.
SCORING_REPLAY_API_KEY (or MINIMAX_API_KEY) supplies the subscription key.
Neither credentials nor child stderr are written into artifacts.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import secrets
import subprocess
import sys
import tempfile
import shutil
import socket
import uuid

ROOT = Path(__file__).resolve().parents[2]
LIMITS = [
    'Component replay, not Service worker persistence, HTTP API or randomized patient end-to-end validation.',
    'Evidence bundles are synthetic locked fixtures; database retrieval/ranking is not exercised.',
    'Offline uses deterministic claim extraction only; no communication scores or roleplay summary are generated.',
    'Roleplay uses production groundedSummary and groundedRoleplaySummary with explicitly annotated public fixture traces; database trace loading is not exercised.',
    'Roleplay model failure retains production deterministic fallback but remains an error in acceptance metrics.',
    'No answer-comparison model or patient-state weighting is exercised.',
]


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path, value):
    with path.open('x', encoding='utf-8') as out:
        json.dump(value, out, ensure_ascii=False, indent=2)
        out.write('\n')


def psql(env, sql):
    process = subprocess.run([env.get('SCORING_REPLAY_PSQL', 'psql'), '-X', '-q', '-A', '-t', '-v', 'ON_ERROR_STOP=1'],
                             input=sql, text=True, capture_output=True, env=env)
    if process.returncode:
        raise RuntimeError('PostgreSQL operation failed; diagnostic output suppressed')
    return process.stdout.strip()


class TemporaryCluster:
    def __init__(self, pg_bin):
        self.pg_bin = pg_bin
        self.temp = Path(tempfile.mkdtemp(prefix='scoring-pg-'))
        self.data = self.temp / 'data'
        self.started = False
        self.env = {k: v for k, v in os.environ.items()
                    if not k.startswith('PG') and k not in ('DATABASE_URL', 'MODEL_KEY_FILE')}
        self.env.update(PGUSER='scoring_replay', PGDATABASE='postgres', PGCONNECT_TIMEOUT='10',
                        SCORING_REPLAY_PSQL=str(pg_bin / 'psql'))

    def command(self, args):
        process = subprocess.run([str(self.pg_bin / args[0]), *map(str, args[1:])],
                                 env=self.env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if process.returncode:
            raise RuntimeError('Disposable PostgreSQL cluster command failed')

    def start(self):
        self.command(['initdb', '-D', self.data, '-A', 'trust', '-U', 'scoring_replay', '--no-locale', '-E', 'UTF8'])
        if os.name == 'nt':
            with socket.socket() as listener:
                listener.bind(('127.0.0.1', 0))
                port = listener.getsockname()[1]
            self.env.update(PGHOST='127.0.0.1', PGPORT=str(port))
            options = f'-F -c listen_addresses=127.0.0.1 -p {port}'
        else:
            directory = self.temp / 'socket'
            directory.mkdir(mode=0o700)
            self.env.update(PGHOST=str(directory))
            options = f"-F -c listen_addresses='' -k {directory}"
        self.command(['pg_ctl', '-D', self.data, '-l', self.temp / 'postgres.log', '-o', options, '-w', 'start'])
        self.started = True
        return self.env

    def close(self):
        if self.started:
            self.command(['pg_ctl', '-D', self.data, '-m', 'fast', '-w', 'stop'])
        shutil.rmtree(self.temp)


def probe(binary, selected, live, env):
    process = subprocess.run([str(binary), '--live' if live else '--offline', str(selected)],
                             capture_output=True, text=True, env=env)
    records = []
    for line in process.stdout.splitlines():
        try:
            value = json.loads(line)
            if isinstance(value, dict) and 'caseId' in value:
                records.append(value)
        except json.JSONDecodeError:
            pass
    audit = []
    for line in process.stderr.splitlines():
        try:
            event = json.loads(line)
            if not isinstance(event, dict) or event.get('event') != 'model_call':
                continue
            # Only numeric/boolean transport metadata and known internal error codes are retained.
            safe = {'event': 'model_call'}
            for name in ('attempt', 'callNumber', 'httpStatus', 'latencyMs', 'maxOutputTokens',
                         'contextTokens', 'promptTokenEstimate', 'promptTokens', 'retry'):
                if type(event.get(name)) in (int, float, bool):
                    safe[name] = event[name]
            error = event.get('errorType')
            if isinstance(error, str) and re.fullmatch(r'MODEL_[A-Z_]+', error):
                safe['errorType'] = error
            audit.append(safe)
        except json.JSONDecodeError:
            pass
    return process.returncode, records, audit


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture', type=Path, default=ROOT / 'backend/tests/fixtures/scoring_acceptance.json')
    parser.add_argument('--binary', type=Path, default=ROOT / 'backend/build/scoring_replay_probe')
    parser.add_argument('--run-dir', type=Path, required=True, help='New directory; existing paths are rejected')
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument('--cases', help='Comma-separated exact case IDs')
    selection.add_argument('--groups', help='Comma-separated exact group names')
    parser.add_argument('--live', action='store_true')
    parser.add_argument('--pg-bin', type=Path, default=Path('/usr/lib/postgresql/16/bin'))
    parser.add_argument('--pass-entry', help='Optional pass entry; key is decrypted only in memory')
    parser.add_argument('--max-calls', type=int, help='Required live HTTP-attempt budget, hard maximum 96')
    args = parser.parse_args()
    fixture = json.loads(args.fixture.read_text(encoding='utf-8'))
    if fixture.get('schemaVersion') != 1:
        raise ValueError('Unsupported fixture schema')
    if args.live and (not (args.cases or args.groups) or not args.max_calls or not 1 <= args.max_calls <= 96):
        raise ValueError('Live requires --cases or --groups and --max-calls between 1 and 96')
    if fixture.get('service', {}).get('payload', {}).get('dataOrigin') != 'synthetic' or any(
            entry.get('metadata', {}).get('origin') != 'synthetic' for entry in fixture.get('knowledge', [])):
        raise ValueError('Replay accepts only explicitly synthetic fixture data')
    cases = fixture['cases']
    ids = [item['id'] for item in cases]
    if len(ids) != len(set(ids)):
        raise ValueError('Duplicate case ID')
    if args.cases or args.groups:
        field = 'id' if args.cases else 'group'
        names = set((args.cases or args.groups).split(','))
        unknown = names - {item[field] for item in cases}
        if unknown:
            raise ValueError('Unknown case/group selection')
        cases = [item for item in cases if item[field] in names]
    if not cases or len(cases) > 64 or any(item.get('repetitions', 1) not in (1, 3) for item in cases):
        raise ValueError('Invalid batch size or repetitions')
    binary = args.binary.resolve(strict=True)
    run_dir = args.run_dir.resolve()
    run_dir.mkdir(parents=True, exist_ok=False)
    selected = dict(fixture, cases=cases)
    selected_path = run_dir / 'input.json'
    write_json(selected_path, selected)
    keys = [{'caseId': item['id'], 'repetition': rep, 'mode': item['mode']}
            for item in cases for rep in range(1, item.get('repetitions', 1) + 1)]
    offline_fixture = fixture if args.live else selected
    offline_path = run_dir / 'offline-input.json'
    write_json(offline_path, offline_fixture)
    sources = sorted((ROOT / 'backend/src').glob('*')) + sorted((ROOT / 'backend/migrations').glob('[0-9][0-9][0-9]_*.sql'))
    sources += [Path(__file__), ROOT / 'backend/tests/scoring_replay_probe.cpp', ROOT / 'backend/CMakeLists.txt', ROOT / 'backend/tests/evaluate_scoring_acceptance.py']
    manifest = {'schemaVersion': 1, 'suiteId': fixture['suiteId'], 'runMode': 'live' if args.live else 'offline',
                'fixtureHash': digest(args.fixture), 'inputHash': digest(selected_path),
                'offlineInputHash': digest(offline_path), 'binaryHash': digest(binary),
                'sourceHashes': {str(p.relative_to(ROOT)): digest(p) for p in sources if p.is_file()},
                'expectedRunKeys': keys, 'httpAttemptLimit': args.max_calls if args.live else 0,
                'model': 'MiniMax-M3.1-Flash' if args.live else None, 'limits': LIMITS,
                'gatewayConfig': {'baseUrl': 'https://api.minimax.cn/v1', 'timeoutMs': 600000,
                                  'contextTokens': 8192, 'requestShaping': 'production buildCompletionRequest/structuredCompletion'}}
    write_json(run_dir / 'manifest.json', manifest)
    env = os.environ.copy()
    # Offline must not accidentally acquire model/database access.
    for name in ('DATABASE_URL', 'SCORING_REPLAY_DSN', 'SCORING_REPLAY_API_KEY', 'MINIMAX_API_KEY'):
        env.pop(name, None)
    code, records, offline_audit = probe(binary, offline_path, False, env)
    offline_results = {'schemaVersion': 1, 'suiteId': fixture['suiteId'], 'runMode': 'offline',
                       'results': records, 'exitCode': code, 'limits': LIMITS}
    write_json(run_dir / 'offline.json', offline_results)
    write_json(run_dir / 'offline-audit.json', offline_audit)
    evaluator_spec = importlib.util.spec_from_file_location('scoring_metrics', ROOT / 'backend/tests/evaluate_scoring_acceptance.py')
    evaluator = importlib.util.module_from_spec(evaluator_spec)
    evaluator_spec.loader.exec_module(evaluator)
    offline_metrics = evaluator.evaluate(offline_fixture, offline_results)
    write_json(run_dir / 'offline-metrics.json', offline_metrics)
    failure = 'OFFLINE_GATE_FAILED' if code or any(r['status'] == 'error' for r in records) else (
        'OFFLINE_ACCEPTANCE_FAILED' if offline_metrics['hardFailures'] else None)
    if args.live:
        records = []
    if args.live and not failure:
        cluster = TemporaryCluster(args.pg_bin.resolve())
        admin = None
        name = 'scoring_replay_' + uuid.uuid4().hex
        token = secrets.token_hex(32)
        created = False
        guarded = False
        records = []
        try:
            key = os.environ.get('SCORING_REPLAY_API_KEY') or os.environ.get('MINIMAX_API_KEY')
            if not key and args.pass_entry:
                secret = subprocess.run(['pass', 'show', args.pass_entry], capture_output=True, text=True, timeout=30)
                if secret.returncode == 0 and secret.stdout.splitlines():
                    key = secret.stdout.splitlines()[0].strip()
            if not key:
                raise ValueError('Missing API key')
            admin = cluster.start()
            # Identifier is generated here, never accepted from a flag or DSN.
            psql(admin, f'CREATE DATABASE "{name}";')
            created = True
            target = dict(admin, PGDATABASE=name)
            oid = psql(target, 'SELECT oid FROM pg_database WHERE datname=current_database();')
            if not re.fullmatch(r'\d+', oid):
                raise RuntimeError('Database identity missing')
            psql(target, f'CREATE TABLE scoring_replay_guard(token text NOT NULL, database_oid oid NOT NULL); '
                         f"INSERT INTO scoring_replay_guard VALUES ('{token}', {oid});")
            guarded = True
            spec = importlib.util.spec_from_file_location('replay_migrations', ROOT / 'backend/migrate.py')
            migrations = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(migrations)
            psql(target, migrations.migration_script())
            # libpq uses PG* for secrets; the DSN contains only our generated database name.
            target.update(SCORING_REPLAY_DATABASE=name, SCORING_REPLAY_TOKEN=token,
                          SCORING_REPLAY_DSN=f'dbname={name}', SCORING_REPLAY_API_KEY=key,
                          SCORING_REPLAY_MAX_CALLS=str(args.max_calls))
            code, records, audit = probe(binary, selected_path, True, target)
            write_json(run_dir / 'model-audit.json', audit)
            if code:
                failure = 'PROBE_FAILED'
        except Exception:
            failure = 'LIVE_SETUP_OR_EXECUTION_FAILED'
        finally:
            if created:
                try:
                    if not guarded:
                        # Creation succeeded but marker did not: retain empty DB for inspection.
                        raise RuntimeError('Database guard unavailable')
                    check = psql(dict(admin, PGDATABASE=name),
                                 'SELECT token || \':\' || database_oid::text || \':\' || '
                                 '(SELECT oid::text FROM pg_database WHERE datname=current_database()) '
                                 'FROM scoring_replay_guard;')
                    if check != f'{token}:{oid}:{oid}':
                        raise RuntimeError('Cleanup identity mismatch')
                    psql(admin, f'DROP DATABASE "{name}";')
                except Exception:
                    failure = 'CLEANUP_FAILED_DATABASE_RETAINED'
                    manifest['retainedDatabase'] = name
            try:
                cluster.close()
                manifest.pop('retainedDatabase', None)
            except Exception:
                failure = 'CLUSTER_CLEANUP_FAILED'
                manifest['retainedCluster'] = str(cluster.temp)
    seen = {(r['caseId'], r['repetition'], r['mode']) for r in records}
    for item in keys:
        if (item['caseId'], item['repetition'], item['mode']) not in seen:
            records.append(dict(item, status='not_run', error={'code': failure or 'MISSING_RESULT'},
                                elapsedMs=0, httpAttempts=0, model=None))
    result = {'schemaVersion': 1, 'suiteId': fixture['suiteId'], 'runMode': manifest['runMode'],
              'expectedRunKeys': keys, 'limits': LIMITS, 'results': records, 'failure': failure}
    if 'retainedDatabase' in manifest:
        result['retainedDatabase'] = manifest['retainedDatabase']
    if 'retainedCluster' in manifest:
        result['retainedCluster'] = manifest['retainedCluster']
    write_json(run_dir / 'results.json', result)
    print(f'Replay artifacts: {run_dir}')
    return int(bool(failure) or any(r['status'] in ('error', 'not_run') for r in records))


if __name__ == '__main__':
    try:
        sys.exit(main())
    except Exception as error:
        # Exceptions from drivers/processes can contain DSNs; report only the class.
        print(f'Replay stopped ({type(error).__name__}); no credentials logged.', file=sys.stderr)
        sys.exit(1)
