"""HTTP lifecycle checks, only called by the harness on its own temporary DB."""
from concurrent.futures import ThreadPoolExecutor
import time


def check_api(api, run, psql, database_url, learner, peer, stop_backend, start_backend):
    assert api('GET', '/health')['ready']
    api('GET', '/sessions', expected=401)
    def sql(query):
        return run([psql, '--dbname', database_url, '-X', '-At', '-v', 'ON_ERROR_STOP=1', '-c', query])
    assert sql('SELECT current_database()') == 'litellm_config_test'
    sql("INSERT INTO users(id,role,status) VALUES('portable-peer','learner','active')")
    peer_learner = api('POST', '/auth/switch-learner', {'userId': 'portable-peer'}, learner)['accessToken']
    for prefix, table, message_table, report, job_type, conflict in [
        ('', 'sessions', 'messages', 'evaluation', 'evaluation', 'SESSION_IN_PROGRESS'),
        ('/roleplay', 'roleplay_sessions', 'roleplay_messages', 'summary', 'roleplay-summary', 'ROLEPLAY_SESSION_IN_PROGRESS')]:
        scenarios = api('GET', prefix + '/scenarios', token=learner)['items']
        scenario = next(x['id'] for x in scenarios if not x.get('activeSession'))
        def create(_):
            # Capture only known success/conflict responses with a thread-local request.
            try:
                return api('POST', prefix + '/sessions', {'scenarioId': scenario}, learner, 201)['session']['id']
            except AssertionError as error:
                # api asserts before consuming the response, so do not retry the mutation.
                assert error.args[0] == (prefix + '/sessions', 409, 201), error
                return None
        with ThreadPoolExecutor(max_workers=10) as pool:
            created = list(pool.map(create, range(20)))
        assert sum(x is not None for x in created) == 1
        sid = next(x for x in created if x)
        path = prefix + '/sessions/' + sid
        assert api('POST', prefix + '/sessions', {'scenarioId': scenario}, learner, 409)['code'] == conflict
        api('GET', path, token=peer, expected=403)
        api('GET', path, token=peer_learner, expected=404)
        detail = api('GET', path, token=learner)
        assert detail['session']['id'] == sid
        message = {'clientMessageId': 'portable-idempotent', 'content': '我想了解具体流程。'}
        assert api('POST', path + '/messages', message, learner, 503)['code'] == 'MODEL_NOT_CONFIGURED'
        assert api('POST', path + '/messages', {**message, 'content': '不同内容'}, learner, 409)['code'] == 'IDEMPOTENCY_CONFLICT'
        pending = api('GET', path, token=learner)['pendingMessage']
        assert pending['replyStatus'] == 'failed'
        role = 'user' if not prefix else 'learner_patient'
        sql(f"UPDATE {message_table} SET reply_status='generating',reply_lease_until=NOW()+INTERVAL '180 seconds',"
            f"reply_attempt_token='portable-lease' WHERE session_id='{sid}' AND role='{role}' AND round=1")
        assert api('POST', path + '/messages', message, learner, 409)['code'] in ('SESSION_RESPONSE_PENDING', 'ROLEPLAY_RESPONSE_PENDING')
        sql(f"UPDATE {message_table} SET reply_lease_until=NOW()-INTERVAL '1 second' WHERE session_id='{sid}'")
        assert api('POST', path + '/messages', message, learner, 503)['code'] == 'MODEL_NOT_CONFIGURED'
        # A completed reply fixture enables report state-machine checks without a real provider.
        response_role = 'patient' if not prefix else 'standard_customer'
        sql(f"UPDATE {message_table} SET reply_status='ready',reply_lease_until=NULL,reply_attempt_token=NULL WHERE session_id='{sid}'; "
            f"INSERT INTO {message_table}(id,session_id,role,content,round) VALUES('portable-{sid}','{sid}','{response_role}','test reply',1); "
            f"UPDATE {table} SET current_round=1 WHERE id='{sid}'")
        for _ in range(2):
            api('POST', path + '/finish', {'reason': 'manual'}, learner, 202)
        def failed():
            for _ in range(60):
                result = api('GET', path + '/' + report, token=learner)
                if result['status'] == 'failed':
                    return
                time.sleep(.1)
            raise AssertionError('Report worker did not reach failed state')
        failed()
        assert sql(f"SELECT count(*)||':'||max(generation) FROM ai_jobs WHERE dedupe_key='{job_type}:{sid}'") == '1:1'
        api('POST', path + '/' + report + '/retry', {}, learner, 202)
        failed()
        assert sql(f"SELECT count(*)||':'||max(generation) FROM ai_jobs WHERE dedupe_key='{job_type}:{sid}'") == '1:2'
        stop_backend()
        start_backend()
        assert api('GET', path, token=learner)['session']['status'] == 'completed'
        api('GET', prefix + '/sessions', token=learner)
        # Resume survives a process restart with the same bearer session.
        resumed = api('POST', prefix + '/sessions', {'scenarioId': scenario}, learner, 201)['session']['id']
        stop_backend()
        start_backend()
        assert api('GET', prefix + '/sessions/' + resumed, token=learner)['session']['status'] == 'in_progress'
    health = api('GET', '/health')
    assert health['databasePool']['open'] <= health['databasePool']['maximum']
    assert health['databasePool']['waiting'] == 0 and health['workerRunning']
    print('PASS: both modes, authorization, 20 concurrent creates/mode, idempotency, expired leases, report retries, history/resume/restart')
