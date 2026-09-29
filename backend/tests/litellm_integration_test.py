"""Offline gateway integration. Creates its own temporary PostgreSQL cluster.

No existing database or real provider is used; the loopback HTTP server is a
deterministic LiteLLM-compatible fixture, not a claim of live LiteLLM validation.
"""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--bin-dir', type=Path, default=ROOT / 'backend/build-msvc/Release')
parser.add_argument('--pg-bin', type=Path, default=Path('C:/Program Files/PostgreSQL/18/bin'))
args = parser.parse_args()
(ROOT / 'tmp').mkdir(exist_ok=True)
work = Path(tempfile.mkdtemp(prefix='litellm-validation-', dir=ROOT / 'tmp'))
env = os.environ.copy()
env['PATH'] = str(args.pg_bin) + os.pathsep + env['PATH']
env.pop('PGOPTIONS', None)
env.pop('ORAL_TRAINING_TEST_DATABASE_URL', None)
flags = subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0


def run(command, **kwargs):
    # On Windows a PostgreSQL child can inherit pipe handles after pg_ctl exits.
    # File-backed output avoids waiting for EOF from a long-running server.
    with tempfile.TemporaryFile(mode='w+b') as output:
        result = subprocess.run([str(x) for x in command], env=env, creationflags=flags,
                                stdout=output, stderr=output, timeout=180, **kwargs)
        output.seek(0)
        text = output.read().decode('utf-8', errors='replace')
    if result.returncode:
        raise RuntimeError(f'{command[0]} failed: {text[-4000:]}')
    return text.strip()


def port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


calls = []


class Gateway(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_POST(self):
        assert self.path == '/v1/chat/completions', self.path
        body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        key = 'sk-peer-key-12345' if body['model'] == 'peer-model' else 'sk-offline-key-12345'
        assert self.headers['Authorization'] == 'Bearer ' + key
        assert not {'thinking', 'user_id'} & body.keys()
        calls.append(body)
        if body['model'] == 'worker-owner':
            # A terminal upstream failure lets us verify worker credential routing
            # without manufacturing a valid dental evaluation or charging a model.
            self.send_response(401)
            self.send_header('Content-Length', '0')
            self.end_headers()
            return
        content = json.dumps({'intent': 'clarify', 'evidenceIds': [], 'userMarker': body['model'], 'reply': '请问具体流程是什么？',
                              'emotion': '平静', 'trustLevel': 50, 'shouldEnd': False}, ensure_ascii=False)
        if body.get('messages', [{}])[-1].get('content') == '谢谢':
            content = json.dumps({'reply': '不客气。', 'replyKind': 'conversation', 'evidenceIds': [],
                                  'learningPoints': [], 'complianceBoundary': '', 'shouldEnd': False}, ensure_ascii=False)
        if body['model'] == 'json-repair' and 'response_format' in body:
            content = 'invalid-json'
        payload = json.dumps({'model': 'fixture-model-v1', 'choices': [
            {'finish_reason': 'stop', 'message': {'content': content}}],
            'usage': {'prompt_tokens': 10, 'completion_tokens': 20, 'total_tokens': 30}}).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)


gateway = ThreadingHTTPServer(('127.0.0.1', 0), Gateway)
threading.Thread(target=gateway.serve_forever, daemon=True).start()
pg_port, api_port = port(), port()
database_url = f'postgresql://postgres@127.0.0.1:{pg_port}/litellm_config_test'
base = f'http://127.0.0.1:{api_port}/api'
process = None
pg_started = False
logs = []


def api(method, path, data=None, token='', expected=200):
    headers = {'Content-Type': 'application/json'}
    if token:
        headers['Authorization'] = 'Bearer ' + token
    request = urllib.request.Request(base + path, data=None if data is None else json.dumps(data).encode(),
                                     headers=headers, method=method)
    try:
        response = urllib.request.urlopen(request, timeout=10)
    except urllib.error.HTTPError as error:
        response = error
    assert response.status == expected, (path, response.status, expected)
    result = json.load(response)
    assert not any(key in json.dumps(result) for key in ['sk-offline-key', 'sk-peer-key']), 'API leaked key'
    return result.get('data') if expected < 300 or path == '/health' else result


def start_backend():
    global process
    log = open(work / f'backend-{len(logs)}.log', 'w', encoding='utf-8')
    logs.append(log)
    process = subprocess.Popen([str(args.bin_dir / 'oral_training_backend.exe')], env=env,
                               creationflags=flags, stdout=log, stderr=log)
    for _ in range(80):
        if process.poll() is not None:
            raise RuntimeError('Backend failed to start: ' + str(work))
        try:
            urllib.request.urlopen(base + '/health', timeout=1)
            return
        except urllib.error.HTTPError as error:
            if error.code == 503:
                return
        except OSError:
            pass
        time.sleep(.15)
    raise RuntimeError('Backend startup timeout')


def stop_backend():
    global process
    if process and process.poll() is None:
        process.terminate()
        process.wait(timeout=10)
    process = None


try:
    run([args.pg_bin / 'initdb.exe', '-D', work / 'pgdata', '-A', 'trust', '-U', 'postgres', '--no-locale', '-E', 'UTF8'])
    run([args.pg_bin / 'pg_ctl.exe', '-D', work / 'pgdata', '-l', work / 'postgres.log',
         '-o', f'-h 127.0.0.1 -p {pg_port}', '-w', 'start'])
    pg_started = True
    run([args.pg_bin / 'createdb.exe', '-h', '127.0.0.1', '-p', pg_port, '-U', 'postgres', 'litellm_config_test'])
    migrate = ['pwsh', '-NoProfile', '-File', ROOT / 'backend/migrate.ps1', '-DatabaseUrl', database_url,
               '-PsqlPath', args.pg_bin / 'psql.exe']
    run(migrate)
    run(migrate)  # Ordered runner is repeatable.
    # Direct rerun of the new additive migration is also safe with existing history.
    run([args.pg_bin / 'psql.exe', database_url, '-X', '-v', 'ON_ERROR_STOP=1', '-f',
         ROOT / 'backend/migrations/034_litellm_settings.sql'])
    env.update(DATABASE_URL=database_url, PRODUCTION='false', AUTH_MODE='demo', REQUIRE_HTTPS='false',
               BIND_ADDRESS='127.0.0.1', PORT=str(api_port), ALLOWED_ORIGIN='*', AI_WORKER_CONCURRENCY='1',
               KNOWLEDGE_WORKER_CONCURRENCY='1', DATABASE_POOL_SIZE='8', RATE_LIMIT_PER_MINUTE='5000',
               DEEPSEEK_API_KEY='ignored-system-key', DEEPSEEK_MODEL='ignored-system-model',
               ALLOW_RUNTIME_API_KEY='true')
    start_backend()
    assert api('GET', '/health')['modelConfigurationScope'] == 'personal'
    api('GET', '/config/litellm', expected=401)
    learner = api('POST', '/auth/wechat', {'code': 'offline-model-config'})['accessToken']
    run([args.pg_bin / 'psql.exe', database_url, '-X', '-v', 'ON_ERROR_STOP=1', '-c',
         "INSERT INTO users(id,display_name,role,status) VALUES('litellm-test-admin','Model test admin','admin','active')"])
    admin = api('POST', '/auth/switch-learner', {'userId': 'litellm-test-admin'}, learner)['accessToken']
    assert api('GET', '/config/litellm', token=learner)['canEdit']
    assert not api('GET', '/config/litellm', token=learner)['configured'], 'Old environment key must be ignored'
    api('PUT', '/config/litellm', {}, learner, 400)
    api('DELETE', '/config/litellm', {}, learner, 400)
    api('POST', '/config/deepseek-key', {'apiKey': 'obsolete'}, admin, 410)
    config = {'revision': 0, 'baseUrl': f'http://127.0.0.1:{gateway.server_port}/v1',
              'model': 'ds-primary', 'apiKey': 'sk-offline-key-12345'}
    api('PUT', '/config/litellm', {**config, 'baseUrl': 'http://remote.example/v1'}, learner, 400)
    saved = api('PUT', '/config/litellm', {**config, 'userId': 'litellm-test-admin'}, learner)
    assert saved['configured'] and saved['revision'] == 1 and 'apiKey' not in saved
    api('PUT', '/config/litellm', config, learner, 409)
    assert not api('GET', '/config/litellm?userId=demo-user-001', token=admin)['configured'], 'Forged userId must be ignored'
    peer = api('PUT', '/config/litellm', {**config, 'model': 'peer-model', 'apiKey': 'sk-peer-key-12345'}, admin)
    assert peer['revision'] == 1 and api('GET', '/config/litellm', token=learner)['model'] == 'ds-primary'
    assert len(calls) == 0, 'Saving must not invoke a model'
    run([args.pg_bin / 'psql.exe', database_url, '-X', '-v', 'ON_ERROR_STOP=1', '-f',
         ROOT / 'backend/migrations/035_personal_litellm_settings.sql'])
    assert api('GET', '/config/litellm', token=learner)['revision'] == 1
    stop_backend()
    start_backend()
    assert api('GET', '/config/litellm', token=learner)['configured'], 'Restart must retain personal config'
    env['LITELLM_TEST_DATABASE_URL'] = database_url
    run([args.bin_dir / 'litellm_settings_test.exe'])
    assert len(calls) == 7 and calls[4]['temperature'] == 0 and 'response_format' not in calls[4]
    training = api('POST', '/sessions', {'scenarioId': 'implant-basic'}, learner, 201)['session']['id']
    api('POST', f'/sessions/{training}/messages',
        {'clientMessageId': 'personal-training', 'content': '您好，请问您想先了解哪些流程？'}, learner)
    roleplay = api('POST', '/roleplay/sessions', {'scenarioId': 'implant-basic'}, learner, 201)['session']['id']
    api('POST', f'/roleplay/sessions/{roleplay}/messages',
        {'clientMessageId': 'personal-roleplay', 'content': '我想了解种植牙的咨询流程。'}, learner)
    assert len(calls) == 9 and all(call['model'] == 'ds-primary' for call in calls[7:])
    social = api('POST', f'/roleplay/sessions/{roleplay}/messages',
                 {'clientMessageId': 'personal-social', 'content': '谢谢'}, learner)['standardCustomerMessage']
    assert social['content'] == '不客气。' and social['learningPoints'] == [] and not social['complianceBoundary']
    persisted = api('GET', f'/roleplay/sessions/{roleplay}', token=learner)['messages'][-1]
    assert persisted['content'] == social['content'] and persisted['learningPoints'] == []
    worker_config = api('GET', '/config/litellm', token=learner)
    api('PUT', '/config/litellm', {**config, 'revision': worker_config['revision'], 'model': 'worker-owner'}, learner)
    api('POST', f'/sessions/{training}/finish', {'reason': 'manual'}, learner, 202)
    api('POST', f'/roleplay/sessions/{roleplay}/finish', {'reason': 'manual'}, learner, 202)
    for _ in range(80):
        if sum(call['model'] == 'worker-owner' for call in calls) >= 2:
            break
        time.sleep(.1)
    assert sum(call['model'] == 'worker-owner' for call in calls) == 2, 'Background reports must use session owner credentials'
    for _ in range(40):
        evaluation = api('GET', f'/sessions/{training}/evaluation', token=learner)
        summary = api('GET', f'/roleplay/sessions/{roleplay}/summary', token=learner)
        if evaluation['status'] == 'failed' and summary['status'] == 'failed':
            break
        time.sleep(.1)
    assert evaluation['status'] == 'failed' and summary['status'] == 'failed'
    # Upstream's plan-generation route must select the requesting supervisor's
    # personal gateway, even though its input profile belongs to a learner.
    run([args.pg_bin / 'psql.exe', database_url, '-X', '-v', 'ON_ERROR_STOP=1', '-c',
         "INSERT INTO supervisor_team_members(learner_id,supervisor_id) VALUES('demo-user-001','litellm-test-admin') "
         "ON CONFLICT(learner_id) DO UPDATE SET supervisor_id=EXCLUDED.supervisor_id"])
    plan_call_start = len(calls)
    api('POST', '/supervisor/training-plans/suggest', {'learnerIds': ['demo-user-001']}, admin, 201)
    assert len(calls) > plan_call_start and all(call['model'] == 'peer-model' for call in calls[plan_call_start:]), 'Plan generation must use the supervisor gateway'
    saved = api('GET', '/config/litellm', token=learner)
    api('PUT', '/config/litellm', {**config, 'revision': saved['revision'], 'apiKey': '',
                                 'baseUrl': 'https://other.example/v1'}, learner, 400)
    raw = run([args.pg_bin / 'psql.exe', database_url, '-X', '-Atc', 'SELECT api_key_encrypted FROM user_model_gateway_settings'])
    assert raw and 'sk-offline' not in raw
    cleared = api('DELETE', '/config/litellm', {'revision': saved['revision'], 'userId': 'litellm-test-admin'}, learner)
    assert not cleared['configured'] and api('GET', '/health')['ready']
    assert api('GET', '/config/litellm', token=admin)['configured'], 'Clearing one account must not affect another'
    stop_backend()
    start_backend()
    assert not api('GET', '/config/litellm', token=learner)['configured']
    assert api('GET', '/config/litellm', token=admin)['configured']
    # Migration from a populated 022 config assigns only its author and retains
    # source rows. Reapply must not resurrect a cleared personal credential.
    run([args.pg_bin / 'psql.exe', database_url, '-X', '-v', 'ON_ERROR_STOP=1', '-c',
         "INSERT INTO users(id,role) VALUES('litellm-migration-owner','learner'); "
         "UPDATE model_gateway_settings SET updated_by='litellm-migration-owner',base_url='https://old.example/v1', "
         "model='legacy',api_key_encrypted='ciphertext-preserved',revision=9"])
    migration23 = [args.pg_bin / 'psql.exe', database_url, '-X', '-v', 'ON_ERROR_STOP=1', '-f',
                   ROOT / 'backend/migrations/035_personal_litellm_settings.sql']
    run(migration23)
    check = run([args.pg_bin / 'psql.exe', database_url, '-X', '-Atc',
                 "SELECT count(*) FROM user_model_gateway_settings WHERE user_id='litellm-migration-owner' AND revision=9"])
    assert check == '1'
    migrated_user = api('POST', '/auth/switch-learner', {'userId': 'litellm-migration-owner'}, learner)['accessToken']
    api('DELETE', '/config/litellm', {'revision': 9}, migrated_user)
    run(migration23)
    assert not api('GET', '/config/litellm', token=migrated_user)['configured']
    for script in ['smoke.ps1', 'state_machine.ps1', 'session_concurrency.ps1']:
        command = ['pwsh', '-NoProfile', '-File', ROOT / 'backend/tests' / script, '-BaseUrl', base]
        if script != 'smoke.ps1':
            command += ['-DatabaseUrl', database_url, '-PsqlPath', args.pg_bin / 'psql.exe']
        run(command)
    run(['pwsh', '-NoProfile', '-File', ROOT / 'backend/tests/patient_initialization.ps1',
         '-DatabaseUrl', database_url, '-PsqlPath', args.pg_bin / 'psql.exe',
         '-ExecutablePath', args.bin_dir / 'patient_initialization_test.exe'])
    print('PASS: v2 dynamic hint/privacy/quota, personal migration/reapply, user isolation/spoof rejection, encrypted persistence/restart, '
          'no env fallback, concurrent user gateway routing, JSON repair, isolated clearing, '
          'smoke/state-machine/concurrency. Artifacts: ' + str(work))
finally:
    stop_backend()
    for log in logs:
        log.close()
    gateway.shutdown()
    gateway.server_close()
    if pg_started:
        run([args.pg_bin / 'pg_ctl.exe', '-D', work / 'pgdata', '-m', 'fast', '-w', 'stop'])
