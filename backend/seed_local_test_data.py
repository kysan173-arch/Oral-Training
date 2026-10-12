#!/usr/bin/env python3
"""Add synthetic UI/RAG fixtures to the configured local demo database. Never reset data."""
import argparse
from datetime import date, datetime, timedelta
import hashlib
import json
import math
from pathlib import Path
import subprocess
import time
from urllib.parse import unquote, urlsplit
import urllib.error
import urllib.request
from zoneinfo import ZoneInfo

from linux_runtime import load_env

ROOT = Path(__file__).resolve().parents[1]
PREFIX = 'local-test-v1-'
ADMIN = PREFIX + 'admin'
LEARNERS = ['demo-user-001'] + [PREFIX + 'learner-' + str(i) for i in range(1, 4)]
EMPTY_LEARNER = PREFIX + 'learner-empty'
MODEL = 'synthetic-local-fixture-v1'


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':'))


def digest(value):
    return hashlib.sha256(canonical(value).encode()).hexdigest()


def sql(value):
    if value is None:
        return 'NULL'
    if isinstance(value, bool):
        return 'TRUE' if value else 'FALSE'
    if isinstance(value, (int, float)):
        return str(value)
    if isinstance(value, (dict, list)):
        return sql(canonical(value)) + '::jsonb'
    return "'" + str(value).replace("'", "''") + "'"


def insert(table, row):
    return f"INSERT INTO {table} ({','.join(row)}) VALUES ({','.join(sql(v) for v in row.values())}) ON CONFLICT DO NOTHING;"


class Database:
    def __init__(self, env, expected):
        uri = urlsplit(env['DATABASE_URL'])
        if uri.scheme not in ('postgresql', 'postgres') or uri.hostname not in ('127.0.0.1', 'localhost', '::1'):
            raise RuntimeError('Only a loopback PostgreSQL connection is allowed')
        if uri.query or unquote(uri.path[1:]) != expected:
            raise RuntimeError('Database name must match --database, without URI query overrides')
        self.env = {k: v for k, v in env.items() if not k.startswith('PG') and k != 'DATABASE_URL'}
        self.env.update(PGHOST=uri.hostname, PGPORT=str(uri.port or 5432), PGDATABASE=expected,
                        PGUSER=unquote(uri.username or ''), PGPASSWORD=unquote(uri.password or ''),
                        PGCONNECT_TIMEOUT='5', PGAPPNAME='oral-local-test-seed')

    def run(self, command, readonly=False):
        env = dict(self.env, PGOPTIONS='-c statement_timeout=30000 -c lock_timeout=5000' +
                   (' -c default_transaction_read_only=on' if readonly else ''))
        result = subprocess.run(['psql', '-X', '-qAt', '-v', 'ON_ERROR_STOP=1'], input=command,
                                text=True, capture_output=True, env=env, timeout=90)
        if result.returncode:
            raise RuntimeError('Database operation failed: ' + result.stderr[-2500:])
        return result.stdout.strip()

    def rows(self, query):
        return json.loads(self.run(f"SELECT COALESCE(json_agg(q),'[]'::json) FROM ({query}) q", True))

    def transaction(self, commands):
        self.run("BEGIN; SET LOCAL standard_conforming_strings=on; SELECT pg_advisory_xact_lock(20261012,1);\n" +
                 '\n'.join(commands) + '\nCOMMIT;')


class Api:
    def __init__(self, port):
        self.base = f'http://127.0.0.1:{port}/api'
        self.http = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        self.tokens = []

    def call(self, method, path, data=None, token='', expected=200):
        headers = {'Content-Type': 'application/json'}
        if token:
            headers['Authorization'] = 'Bearer ' + token
        request = urllib.request.Request(self.base + path, headers=headers, method=method,
                                         data=None if data is None else canonical(data).encode())
        for attempt in range(3):
            try:
                response = self.http.open(request, timeout=90)
            except urllib.error.HTTPError as error:
                response = error
            if response.status == 429 and attempt < 2:
                response.close()
                print('Waiting for the local API rate-limit window.', flush=True)
                time.sleep(35)
                continue
            with response:
                body = json.load(response)
                if response.status != expected:
                    raise RuntimeError(f'API {method} {path}: {response.status}, {body.get("message", "")}')
                return body.get('data', body)
        raise RuntimeError('Local API rate limit did not clear')

    def login(self, user):
        token = self.call('POST', '/auth/switch-learner', {'userId': user})['accessToken']
        self.tokens.append(hashlib.sha256(token.encode()).hexdigest())
        return token


def unknown(reason='测试资料尚未录入，不能据此推测。'):
    return {'status': 'unknown', 'reason': reason}


def duration(low, high, unit, conditions):
    return dict(status='known', minimum=low, maximum=high, unit=unit, estimated=True, conditions=conditions)


def service_specs(scenarios):
    common = dict(dataOrigin='synthetic', includedItems=[], excludedItems=[], professionalTopics=[],
                  appointment=unknown('没有实时号源，客服不能代为查号或占位。'),
                  price=unknown(), visitDuration=unknown(), treatmentDuration=unknown(), followupInterval=unknown())
    def spec(key, name, category, ids, **fields):
        return dict(key=key, payload=dict(common, name='[测试] ' + name, category=category,
                    scenarioIds=[i for i in ids if i in scenarios], **fields))
    return [
        spec('implant', '种植牙基础档示例方案', '种植修复',
             [i for i in scenarios if i not in ('orthodontic-basic', 'orthodontic-option')],
             price=dict(status='known', type='fixed', currency='CNY', unit='per_tooth', amountMinor=890000,
                        conditions='仅基础档组合示例，最终需医生检查确认；拔牙、植骨及牙周治疗另计'),
             includedItems=['基础档种植体', '标准基台', '国产全瓷冠', '种植手术服务', '一次CBCT'],
             excludedItems=['拔牙', '植骨', '牙周治疗'],
             visitDuration=duration(30, 45, 'minute', '初诊检查与方案沟通'),
             treatmentDuration=duration(3, 8, 'month', '受骨量及愈合情况影响，需医生检查后确认'),
             followupInterval=duration(6, 12, 'month', '戴冠后首次复查为1个月，此后由医生调整间隔')),
        spec('ortho', '隐形矫正咨询方案', '正畸', ['orthodontic-basic', 'orthodontic-option'],
             price=dict(status='known', type='range', currency='CNY', unit='per_case', minimumMinor=1800000,
                        maximumMinor=3200000, conditions='虚构演示区间，具体需医生检查确认，保持器另计'),
             includedItems=['方案内矫治器', '方案内常规复诊'], excludedItems=['拔牙', '保持器'],
             visitDuration=duration(45, 60, 'minute', '初诊检查与资料采集'),
             treatmentDuration=duration(12, 24, 'month', '虚构参考范围，不保证个体完成时间'),
             followupInterval=duration(4, 8, 'week', '具体按医生制定的复诊计划')),
        spec('cleaning', '洁牙体验方案', '口腔预防', ['free-roleplay-template', 'price-comparison', 'tpl-consultation-1', 'tpl-recommendation-1'],
             price=dict(status='known', type='fixed', currency='CNY', unit='per_visit', amountMinor=28000,
                        conditions='虚构演示价，检查确认适用后执行；不包含牙周深部治疗'),
             includedItems=['常规洁治', '口腔清洁指导'], excludedItems=['牙周深部治疗'],
             visitDuration=duration(30, 60, 'minute', '以口腔情况及医生评估为准')),
        spec('missing', '资料待完善服务', '边界测试', ['implant-basic', 'price-comparison']),
        dict(spec('draft', '待发布服务草稿', '草稿测试', ['implant-basic']), draft=True),
        dict(spec('archived', '已下架服务样本', '归档测试', ['implant-basic']), archived=True),
    ]


def knowledge_specs(services):
    corpus = json.loads((ROOT/'docs/rag-data/患者模拟_RAG测试知识库_具体参数版.json').read_text())
    output = [dict(item, service='implant') for item in corpus['knowledgeItems']]
    extra = [
        ('ortho', 'ortho-cost', '隐形矫正费用', '本合成测试方案参考费用18000～32000元/例，需医生检查后确认。包含方案内矫治器和常规复诊，拔牙及保持器另计，新增项目实施前需确认。'),
        ('ortho', 'ortho-time', '隐形矫正周期', '本合成测试方案初诊45～60分钟，完整疗程参考12～24个月，不能保证按指定日期完成。复诊参考4～8周，由医生按方案调整。'),
        ('ortho', 'ortho-assessment', '矫正检查边界', '是否适合隐形矫正、是否需要拔牙，都必须由医生结合检查和影像评估。客服不能仅凭聊天作决定，也不能承诺无痛或固定结果。'),
        ('ortho', 'ortho-followup', '矫正复诊与保持', '患者应按医生制定的计划复诊；矫治器不适或破损应联系接诊医生确认。保持器属于另计项目，具体方案和费用需确认，客服不能编造价格或调整佩戴方案。'),
        ('cleaning', 'cleaning-cost', '洁牙费用与项目', '本虚构洁牙体验方案280元/次，包含常规洁治与口腔清洁指导，不含牙周深部治疗。是否适用需医生检查确认，不应把洁牙宣传为治疗所有牙周问题。'),
        ('cleaning', 'cleaning-duration', '洁牙就诊时间', '本虚构方案单次通常预留30～60分钟，具体视口腔情况及医生评估。客服不能保证完全不敏感、不疼或清除所有色素。'),
        (None, 'booking-boundary', '预约能力边界', '本测试系统不连接实时号源。客服不能查询或锁定具体名额，也不能声称已联系、预约或安排。可以解释已发布的服务资料，并说明号源需通过门诊实际预约渠道确认。'),
        (None, 'communication-boundary', '患者沟通与隐私', '合成训练中先了解患者关注，再解释有依据的服务资料。不把客服沟通当成医生诊断，不要求无关家庭住址，不贬低其他机构。对明显加重的不适提示联系医生，不擅自指导加药。'),
    ]
    for service, topic, title, body in extra:
        output.append(dict(service=service, topic=topic, title=title, body=body,
                           metadata=dict(origin='synthetic', verification='unverified', trainingScope='demo',
                                         applicability='仅用于本地虚构客服训练与功能验证。', aliases=[], sourceUrl=None)))
    output.append(dict(service='implant', topic='draft-unpublished', title='待审核知识草稿',
                       body='这是尚未发布的合成草稿，不能被会话检索。', draft=True,
                       metadata=dict(origin='synthetic', verification='unverified', trainingScope='demo',
                                     applicability='仅用于草稿可见性测试。', aliases=[], sourceUrl=None)))
    return output


def publish_catalog(db, api, token, scenarios):
    services = {}
    for spec in service_specs(scenarios):
        payload = spec['payload']
        found = db.rows(f"SELECT id,current_revision_id,status FROM clinic_services WHERE created_by={sql(ADMIN)} AND name={sql(payload['name'])}")
        if len(found) > 1:
            raise RuntimeError('Duplicate seed service name; inspect before continuing')
        if not found:
            api.call('POST', '/admin/services', {'payload': payload}, token, 201)
            found = db.rows(f"SELECT id,current_revision_id,status FROM clinic_services WHERE created_by={sql(ADMIN)} AND name={sql(payload['name'])}")
        item = found[0]
        if not item['current_revision_id'] and not spec.get('draft'):
            draft = api.call('GET', f"/admin/services/{item['id']}/draft", token=token)
            api.call('POST', f"/admin/services/{item['id']}/publish",
                     {'draftVersion': draft['draftVersion'], 'idempotencyKey': PREFIX + 'service-' + spec['key']}, token)
        if spec.get('archived') and item['status'] != 'archived':
            api.call('POST', f"/admin/services/{item['id']}/archive", {}, token)
        services[spec['key']] = db.rows(f"SELECT s.id,s.current_revision_id AS revision,r.payload FROM clinic_services s LEFT JOIN service_revisions r ON r.id=s.current_revision_id WHERE s.id={sql(item['id'])}")[0]
    for spec in knowledge_specs(services):
        sid = services[spec['service']]['id'] if spec['service'] else None
        found = db.rows(f"SELECT id,current_revision_id FROM knowledge_entries WHERE created_by={sql(ADMIN)} AND topic={sql(spec['topic'])} AND service_id IS NOT DISTINCT FROM {sql(sid)}")
        if len(found) > 1:
            raise RuntimeError('Duplicate seed knowledge topic; inspect before continuing')
        if not found:
            payload = {k: spec[k] for k in ['topic', 'title', 'body', 'metadata']}
            payload.update(scope='service' if sid else 'general', serviceId=sid or '')
            api.call('POST', '/admin/knowledge', payload, token, 201)
            found = db.rows(f"SELECT id,current_revision_id FROM knowledge_entries WHERE created_by={sql(ADMIN)} AND topic={sql(spec['topic'])} AND service_id IS NOT DISTINCT FROM {sql(sid)}")
        if not found[0]['current_revision_id'] and not spec.get('draft'):
            eid = found[0]['id']
            draft = api.call('GET', f'/admin/knowledge/{eid}/draft', token=token)
            api.call('POST', f'/admin/knowledge/{eid}/publish',
                     {'draftVersion': draft['draftVersion'], 'idempotencyKey': PREFIX + 'knowledge-' + spec['topic']}, token)
    return services


DIALOGUE = [
    ('您好，您现在最担心价格、疼痛还是时间？', '我怕疼，也想先了解大概费用。'),
    ('不能保证完全不疼。费用要按方案核对，不能只看种植体单价。', '那报价里还有哪些项目？'),
    ('基础档组合示例约8900元/颗，含种植体、基台、牙冠、手术费及一次CBCT，拔牙植骨等另计，需医生检查确认。', '明白了，我先安排时间检查。'),
    ('初诊可预留30～45分钟，具体号源请通过门诊预约渠道确认。', '好的，我先考虑一下，谢谢。'),
]


def report(score, dialogue, risk=False, unscored=False, manifest=None):
    rewrite = '理解您担心疼痛，不能保证完全无痛，具体请医生结合检查评估。'
    value = dict(schemaVersion=2 if unscored else 1, totalScore=None if unscored else score,
                 passed=None if unscored else score >= 60, summary='[合成测试报告] 用于查看评分、复盘与学习记录，不是真实模型评分。',
                 dimensionScores={k: score for k in ['knowledgeAccuracy', 'medicalCompliance', 'empathy', 'needsDiscovery', 'serviceEtiquette']},
                 strengths=[dict(round=3, content='说明示例报价包含项目与检查条件。', evidence=dialogue[2][0])],
                 improvements=[dict(round=1, content='先回应患者的担忧，再说明能力边界。')],
                 violations=[dict(round=1, originalQuote=dialogue[0][0], reason='不应作绝对化承诺。', deduction=30, recommendedRewrite=rewrite)] if risk else [],
                 roundComments=[dict(round=i+1, userMessage=pair[0], comment='合成点评：保留边界并关注患者需求。', recommendedRewrite=rewrite if i == 0 else pair[0]) for i, pair in enumerate(dialogue)],
                 recommendedPhrases=[dict(phraseKey='phrase-empathy', round=1, patientSays='我怕疼。', csReply=rewrite, reason='先共情，再明确医疗边界。')],
                 learningMistakes=[dict(mistakeKey='mistake-boundary', kind='violation' if risk else 'expression', priority='high' if risk else 'medium', round=1, originalQuote=dialogue[0][0], reason='练习先共情、再说明边界。', recommendedRewrite=rewrite)],
                 modelVersion=MODEL, promptVersion=MODEL)
    if unscored:
        value['dimensionScores']['knowledgeAccuracy'] = None
        value.update(knowledgeAssessment=dict(status='insufficient_evidence', knowledgeAccuracy=None, assessableCount=0, unassessableCount=1, coverage=0),
                     knowledgeChecks=[], knowledgeManifestHash=manifest)
    return value


def seed_histories(db, services, scenarios, as_of):
    commands = []
    base = datetime.combine(as_of, datetime.min.time(), ZoneInfo('Asia/Shanghai')) + timedelta(hours=10)
    manifests = {}
    for key, service in services.items():
        if service['revision']:
            manifests[key] = [r['current_revision_id'] for r in db.rows(f"""SELECT k.current_revision_id FROM knowledge_entries k
                JOIN knowledge_revisions r ON r.id=k.current_revision_id
                WHERE k.status='active' AND r.metadata->>'trainingScope'='demo'
                AND COALESCE(NULLIF(r.metadata->>'effectiveFrom',''),'0001-01-01') <= to_char(now() AT TIME ZONE 'Asia/Shanghai','YYYY-MM-DD')
                AND COALESCE(NULLIF(r.metadata->>'effectiveUntil',''),'9999-12-31') >= to_char(now() AT TIME ZONE 'Asia/Shanghai','YYYY-MM-DD')
                AND (k.scope='general' OR k.service_id={sql(service['id'])}) ORDER BY k.current_revision_id""")]

    def context(sid, mode, service_key, moment):
        service = services[service_key]; manifest = manifests[service_key]
        mh = 'sha256:' + digest(dict(version=1, serviceRevisionId=service['revision'], knowledgeRevisionIds=sorted(set(manifest)), trainingScope='demo'))
        state = dict(emotion='犹豫', emotionLevel=0, trustLevel=50, revealedInformation=[], triggeredObjections=[], riskTriggered=False, endingReason=None)
        row = dict(id=sid+'-ctx', session_type=mode, session_id=sid, service_id=service['id'], service_revision_id=service['revision'],
                   manifest=manifest, manifest_hash=mh, training_scope='demo', initialization_status='ready',
                   public_profile=dict(displayName='测试李女士', ageRange='40-49', initialEmotion='犹豫'),
                   private_profile=dict(consultationGoal='了解所选服务的费用与安排', serviceRevisionId=service['revision'], budget='8000', concerns=['价格'],
                                        hiddenInformation=['正在比较另外两家诊所'], objections=['担心费用超出预算'], revealPolicy=dict(budget='only_when_asked', competitor='round_3_or_when_asked')),
                   patient_state=state, initialized_at=moment, initialization_model_version=MODEL)
        return insert('training_contexts', row), dict(contextId=row['id'], serviceId=service['id'], serviceRevisionId=service['revision'],
                     manifest=manifest, manifestHash=mh, trainingScope='demo'), state

    def group(table, sid, statements):
        commands.append(f"DO $seed$ BEGIN IF NOT EXISTS(SELECT 1 FROM {table} WHERE id={sql(sid)}) THEN\n" + '\n'.join(statements) + '\nEND IF; END $seed$;')

    for person, user in enumerate(LEARNERS):
        scores = ([66, 72, 78, 83, 88, 91] if person < 2 else [48, 55, 61, 64, 69, 73])
        for index, kind in enumerate(['ready'] * 6 + ['failed', 'abandoned', 'active', 'unscored']):
            sid = f'{PREFIX}training-{person}-{index}'
            is_v2 = kind in ('active', 'unscored')
            key = 'missing' if kind == 'unscored' else 'implant'
            start = base - timedelta(days=max(0, 7-index), minutes=30)
            status = 'in_progress' if kind == 'active' else 'abandoned' if kind == 'abandoned' else 'completed'
            dialogue = [list(p) for p in DIALOGUE]
            risk = person >= 2 and index < 2
            if risk:
                dialogue[0][0] = '我们保证百分之百成功，绝对不会疼。'
                dialogue[0][1] = '说得这么肯定，我反而不放心。'
            if kind == 'unscored':
                dialogue = [['该服务费用暂未确认，我不能给出报价。', '那我先了解检查流程。'],
                            ['具体适用情况需医生检查后确认。', '我也想问一下时间安排。'],
                            ['这项服务时长资料还未完善，暂时无法确认。', '明白，我再考虑。'],
                            ['好的，有需要再咨询。', '谢谢，再见。']]
            count = 2 if kind in ('active', 'abandoned') else 4
            state = dict(emotion='犹豫' if kind == 'active' else '平静', emotionLevel=0, trustLevel=40 if risk else 60)
            score = scores[index] if index < 6 else None
            evaluation = 'ready' if kind in ('ready', 'unscored') else 'failed' if kind == 'failed' else 'not_started'
            row = dict(id=sid, user_id=user, scenario_id='implant-basic', scenario_name=scenarios['implant-basic'], status=status,
                       current_round=count, max_rounds=10, patient_state=state, started_at=start.isoformat(), updated_at=(start+timedelta(minutes=10)).isoformat(),
                       finished_at=None if status=='in_progress' else (start+timedelta(minutes=10)).isoformat(), evaluation_status=evaluation, total_score=score,
                       custom_patient_profile=dict(description='合成本地测试患者'), difficulty_tier='standard')
            items = []
            ctx = None
            if is_v2:
                row.update(service_id=services[key]['id'], service_revision_id=services[key]['revision'], context_version=2, client_session_id=sid)
                context_sql, ctx, state = context(sid, 'training', key, start.isoformat())
                row['patient_state'] = state
            items.append(insert('sessions', row))
            if is_v2:
                items.append(context_sql)
            items.append(insert('messages', dict(id=sid+'-opening', session_id=sid, role='patient', content='你好，我想了解一下这个服务的费用和安排。', round=0, created_at=start.isoformat())))
            for number, pair in enumerate(dialogue[:count], 1):
                for role, content, seconds in [('user', pair[0], 0), ('patient', pair[1], 15)]:
                    items.append(insert('messages', dict(id=f'{sid}-{number}-{role}', session_id=sid, role=role, content=content, round=number,
                                 client_message_id=f'{sid}-{number}' if role=='user' else None, reply_status='ready' if role=='user' else None,
                                 emotion=None if role=='user' else '平静', created_at=(start+timedelta(minutes=number, seconds=seconds)).isoformat())))
            if evaluation == 'ready':
                items.append(insert('evaluations', dict(session_id=sid, status='ready', report=report(score or 80, dialogue, risk, kind=='unscored', ctx['manifestHash'] if ctx else None),
                             model_version=MODEL, prompt_version=MODEL, generated_at=(start+timedelta(minutes=11)).isoformat())))
            elif evaluation == 'failed':
                items.append(insert('evaluations', dict(session_id=sid, status='failed', error_type='MODEL_NOT_CONFIGURED', model_version=MODEL, prompt_version=MODEL)))
            items.append(insert('session_hints', dict(id=sid+'-hint', session_id=sid, hint_number=1, round=1, content='先了解患者最担心的事情，再说明检查与报价边界。')))
            if index < 2:
                items.append(insert('learner_phrase_favorites', dict(user_id=user, session_id=sid, phrase_key='phrase-empathy')))
                items.append(insert('learner_mistake_progress', dict(user_id=user, session_id=sid, mistake_key='mistake-boundary', mastered_at=base.isoformat() if index == 0 else None)))
            group('sessions', sid, items)

        for index, kind in enumerate(['ready', 'active', 'failed', 'abandoned']):
            sid = f'{PREFIX}roleplay-{person}-{index}'
            moment = (base-timedelta(days=max(0, 2-index))).isoformat()
            context_sql, ctx, state = context(sid, 'roleplay', 'implant', moment)
            items = [insert('roleplay_sessions', dict(id=sid, user_id=user, scenario_id='implant-basic', scenario_name=scenarios['implant-basic'],
                       status='in_progress' if kind=='active' else 'abandoned' if kind=='abandoned' else 'completed', current_round=3, max_rounds=10,
                       service_id=services['implant']['id'], service_revision_id=services['implant']['revision'], client_session_id=sid, context_version=2,
                       started_at=moment, updated_at=moment, finished_at=None if kind=='active' else moment)), context_sql]
            pairs = [('初诊要留多久？', '初诊通常预留30～45分钟，用于检查与方案沟通。', 'visitDuration'),
                     ('完整治疗多久？', '完整疗程参考3～8个月，受骨量及愈合情况影响，需医生检查后确认。', 'treatmentDuration'),
                     ('周六能帮我占位吗？', '我看不到实时号源，也不能代为占位，需要通过门诊预约渠道确认。', None)]
            for number, (question, reply, field) in enumerate(pairs, 1):
                trace = f'{sid}-trace-{number}'
                citation = dict(traceId=trace, evidenceId='F1', revisionId=ctx['serviceRevisionId'], manifestHash=ctx['manifestHash'])
                bundle = dict(ctx, knowledgeAsOf=datetime.now(ZoneInfo('Asia/Shanghai')).isoformat(), purpose='customer_reply',
                              facts=[dict(evidenceId='F1', field=field, value=services['implant']['payload'][field], revisionId=ctx['serviceRevisionId'])] if field else [],
                              passages=[], conflicts=[], missingFields=[] if field else ['appointment'], retrievalStatus='ok' if field else 'no_hit', retrieverVersion=MODEL)
                items.append(insert('rag_traces', dict(id=trace, context_id=ctx['contextId'], purpose='customer_reply', round=number, query=question,
                                                      evidence_json=bundle, model_version=MODEL, latency_ms=0, is_public=True)))
                for role, content in [('learner_patient', question), ('standard_customer', reply)]:
                    items.append(insert('roleplay_messages', dict(id=f'{sid}-{number}-{role}', session_id=sid, role=role, content=content, round=number,
                                 client_message_id=f'{sid}-{number}' if role=='learner_patient' else None, reply_status='ready' if role=='learner_patient' else None,
                                 answer_status=('answered' if field else 'unknown') if role=='standard_customer' else None,
                                 citations=[citation] if role=='standard_customer' and field else [], trace_id=trace if role=='standard_customer' else None,
                                 learning_points=['保留范围和条件，不编造预约动作。'] if role=='standard_customer' else [], created_at=moment)))
            if kind in ('ready', 'failed'):
                items.append(insert('roleplay_summaries', dict(session_id=sid, status=kind,
                             summary=dict(schemaVersion=1, summary='合成复盘', coveredTopics=[], keyPrinciples=[], nextPracticeSuggestions=[]) if kind=='ready' else None,
                             error_type='MODEL_NOT_CONFIGURED' if kind=='failed' else None, model_version=MODEL, prompt_version=MODEL, generated_at=moment)))
            group('roleplay_sessions', sid, items)

        for days in [1, 2, 3, 5, 7, 10, 14]:
            commands.append(insert('learner_checkins', dict(user_id=user, checkin_date=(as_of-timedelta(days=days)).isoformat(), points=10)))

    for key, title, age, due, status, count in [
        ('weekly', '本周咨询沟通练习', 7, 7, 'published', 4),
        ('monthly', '本月医疗边界练习', 28, 20, 'published', 10),
        ('overdue', '已到期复盘样本', 30, -1, 'published', 12),
        ('draft', '待审核提升计划', 1, 7, 'draft', 3),
        ('dismissed', '已忽略计划样本', 2, 7, 'dismissed', 3),
    ]:
        pid = PREFIX + 'plan-' + key
        commands.append(insert('training_plans', dict(id=pid, title='[测试] '+title, period='month' if key=='monthly' else 'week',
                    scenario_ids=['implant-basic'], required_count=count, required_pass_rate=60, description='合成测试计划，仅用于本地功能验证。',
                    due_at=(base+timedelta(days=due)).isoformat(), created_by=ADMIN, created_at=(base-timedelta(days=age)).isoformat(),
                    source='supervisor', status=status, focus_dimension='medicalCompliance', max_per_scenario=0, require_each_pass=False)))
        if status == 'published':
            for user in LEARNERS:
                commands.append(insert('training_assignments', dict(plan_id=pid, learner_id=user, assigned_at=(base-timedelta(days=age)).isoformat(), is_notified=True)))
    db.transaction(commands)


def protected_fingerprints(db):
    result = {}
    for table, condition in [('sessions', f"id NOT LIKE '{PREFIX}%'"), ('messages', f"session_id NOT LIKE '{PREFIX}%'"),
                             ('evaluations', f"session_id NOT LIKE '{PREFIX}%'"), ('user_model_gateway_settings', 'TRUE'), ('model_gateway_settings', 'TRUE')]:
        result[table] = db.run(f"SELECT md5(COALESCE(string_agg(to_jsonb(t)::text,'|' ORDER BY to_jsonb(t)::text),'')) FROM {table} t WHERE {condition}", True)
    return result


def warm_embeddings(db, env):
    base = env.get('RAG_EMBEDDING_BASE_URL', 'http://127.0.0.1:8765/v1')
    endpoint = urlsplit(base)
    if endpoint.scheme != 'http' or endpoint.hostname not in ('127.0.0.1', 'localhost', '::1') or endpoint.query or endpoint.fragment:
        raise RuntimeError('Embedding warm-up requires the local EmbeddingGemma endpoint')
    model = env.get('RAG_EMBEDDING_MODEL', 'embeddinggemma-2')
    dimensions = int(env.get('RAG_EMBEDDING_DIMENSIONS', '768'))
    revision = env.get('RAG_EMBEDDING_REVISION', '1')
    # Same input and configuration key as EmbeddingConfig::cacheKey/embeddingText.
    key = digest([base, model, revision, dimensions, 'document:title-body-applicability-v1'])
    rows = db.rows(f"""SELECT c.id,r.title,c.body,r.metadata,e.input_sha256 FROM knowledge_chunks c
        JOIN knowledge_revisions r ON r.id=c.revision_id JOIN knowledge_entries k ON k.id=r.entry_id
        LEFT JOIN knowledge_chunk_embeddings e ON e.chunk_id=c.id AND e.cache_key={sql(key)}
        WHERE k.created_by={sql(ADMIN)} ORDER BY c.id""")
    pending = []
    for row in rows:
        text = row['title']+'\n'+row['body']+'\n'+row['metadata'].get('applicability','')
        input_hash = hashlib.sha256(text.encode()).hexdigest()
        if row['input_sha256'] != input_hash:
            pending.append((row['id'], text, input_hash))
    http = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    commands = []
    for start in range(0, len(pending), 8):
        batch = pending[start:start+8]
        request = urllib.request.Request(base.rstrip('/')+'/embeddings', method='POST', headers={'Content-Type':'application/json'},
                  data=canonical(dict(model=model, input=[v[1] for v in batch], dimensions=dimensions, task='document', encoding_format='float')).encode())
        with http.open(request, timeout=60) as response:
            body = json.load(response)
        assert body['model']==model and len(body['data'])==len(batch)
        ordered = sorted(body['data'], key=lambda x:x['index'])
        for index, vector in enumerate(ordered):
            assert vector['index']==index and len(vector['embedding'])==dimensions
            values = vector['embedding']
            assert all(isinstance(x,(int,float)) and not isinstance(x,bool) and math.isfinite(x) for x in values)
            norm = math.sqrt(sum(x*x for x in values)); assert math.isfinite(norm) and norm>1e-6
            row = dict(chunk_id=batch[index][0], cache_key=key, input_sha256=batch[index][2], model=model,
                       dimensions=dimensions, embedding=[x/norm for x in values])
            commands.append(insert('knowledge_chunk_embeddings', row))
    # All network work is finished before opening the write transaction.
    if commands:
        db.transaction(commands)
    return dict(model=model, dimensions=dimensions, chunks=len(rows), inserted=len(commands), cacheKey=key)


def validate(db, api, services):
    checked = []
    for user in [LEARNERS[0], LEARNERS[2], EMPTY_LEARNER, ADMIN]:
        token = api.login(user)
        paths = ['/supervisor/dashboard', '/supervisor/training-plans', '/supervisor/training-plan-drafts', '/admin/services', '/admin/knowledge'] if user == ADMIN else [
            '/dashboard/summary', '/sessions', '/roleplay/sessions', '/learning/phrases?favoritesOnly=true', '/learning/mistakes', '/learning/profile', '/learning/mine', '/learning/training-plans']
        for path in paths:
            api.call('GET', path, token=token)
            checked.append(user + ':' + path)
        if user == LEARNERS[0]:
            for suffix, expected in [('0', 'ready'), ('6', 'failed'), ('9', 'ready')]:
                data = api.call('GET', f'/sessions/{PREFIX}training-0-{suffix}/evaluation', token=token)
                assert data['status'] == expected
                if suffix == '9':
                    assert data['evaluation']['totalScore'] is None
            data = api.call('GET', f'/roleplay/sessions/{PREFIX}roleplay-0-0/summary', token=token)
            assert data['status']=='ready' and len(data['summary']['groundedFacts']) == 2
            evidence = api.call('GET', f'/roleplay/sessions/{PREFIX}roleplay-0-0/evidence/{PREFIX}roleplay-0-0-trace-1', token=token)
            assert evidence
            for mode, sid in [('/sessions/', PREFIX+'training-0-8'), ('/roleplay/sessions/', PREFIX+'roleplay-0-1')]:
                resumed = api.call('GET', mode+sid, token=token)
                assert resumed['session']['status']=='in_progress' and resumed['messages']
                checked.append('resume:'+mode+sid)
            init = api.call('GET', f'/sessions/{PREFIX}training-0-8/initialization', token=token)
            assert init['status']=='ready'
            checked.append('initialization:ready')
        if user == LEARNERS[2]:
            api.call('GET', f'/sessions/{PREFIX}training-0-0', token=token, expected=404)
        if user == ADMIN:
            for topic, question in [('implant-cost-components', '种植牙报价包含哪些项目？'), ('ortho-time', '隐形矫正需要多久？')]:
                entry = db.rows(f"SELECT e.id,d.draft_version FROM knowledge_entries e JOIN knowledge_drafts d ON d.entry_id=e.id WHERE e.created_by={sql(ADMIN)} AND e.topic={sql(topic)}")[0]
                preview = api.call('POST', '/admin/knowledge/preview', dict(entityType='knowledge', entityId=entry['id'], draftVersion=entry['draft_version'], question=question), token)
                assert preview['retrievalStatus']=='ok'
                checked.append('embedding-preview:'+topic)
    counts = {table: int(db.run(f'SELECT count(*) FROM {table}', True)) for table in [
        'users', 'scenarios', 'clinic_services', 'service_revisions', 'knowledge_entries', 'knowledge_revisions', 'knowledge_chunks',
        'sessions', 'messages', 'evaluations', 'roleplay_sessions', 'roleplay_messages', 'roleplay_summaries', 'training_contexts', 'rag_traces',
        'training_plans', 'training_assignments', 'learner_checkins', 'learner_phrase_favorites', 'learner_mistake_progress', 'knowledge_chunk_embeddings']}
    assert counts['knowledge_chunks'] >= 20
    assert not db.rows(f"SELECT id FROM ai_jobs WHERE target_id LIKE '{PREFIX}%' AND status IN ('pending','running','retry_wait')")
    return dict(counts=counts, apiChecks=len(checked)+6, services={k:v['id'] for k,v in services.items()})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--env-file', type=Path, default=ROOT/'backend/backend.env')
    parser.add_argument('--database', required=True, help='Exact expected local database name')
    parser.add_argument('--as-of', type=date.fromisoformat, default=datetime.now(ZoneInfo('Asia/Shanghai')).date())
    parser.add_argument('--apply', action='store_true', help='Add fixtures; without this flag only inspect')
    args = parser.parse_args()
    env = load_env(args.env_file)
    if env.get('PRODUCTION','').lower() not in ('false','0','no','off') or env.get('AUTH_MODE') != 'demo':
        raise RuntimeError('Only an explicitly non-production demo configuration is allowed')
    db = Database(env, args.database); api = Api(int(env.get('PORT', '8080')))
    health = api.call('GET', '/health')
    if health['production'] or health['authMode'] != 'demo' or not health['ready']:
        raise RuntimeError('Running backend must be a healthy local demo instance')
    assert db.rows('SELECT current_database() AS name')[0]['name'] == args.database
    assert len(db.rows('SELECT name FROM schema_migrations')) >= 36
    scenarios = {r['id']:r['name'] for r in db.rows('SELECT id,name FROM scenarios')}
    print(json.dumps(dict(database=args.database, host=db.env['PGHOST'], scenarios=len(scenarios), apply=args.apply), ensure_ascii=False), flush=True)
    if not args.apply:
        return
    before = protected_fingerprints(db)
    identities = [(ADMIN, 'Test 主管', 'admin'), (LEARNERS[1], 'Test 学员·稳步提升', 'learner'),
                  (LEARNERS[2], 'Test 学员·待加强', 'learner'), (LEARNERS[3], 'Test 学员·复盘练习', 'learner'),
                  (EMPTY_LEARNER, 'Test 学员·无历史', 'learner')]
    for uid, name, role in identities:
        existing = db.rows(f'SELECT role,is_demo FROM users WHERE id={sql(uid)}')
        if existing and (existing[0]['role'] != role or not existing[0]['is_demo']):
            raise RuntimeError('Seed user ID conflicts with an existing non-fixture account')
    commands = [insert('users', dict(id=uid, display_name=name, role=role, status='active', is_demo=True)) for uid, name, role in identities]
    commands += [insert('supervisor_team_members', dict(learner_id=uid, supervisor_id=ADMIN)) for uid in LEARNERS+[EMPTY_LEARNER]]
    db.transaction(commands)
    try:
        print('Publishing synthetic services and knowledge through the local API.', flush=True)
        services = publish_catalog(db, api, api.login(ADMIN), scenarios)
        print('Adding paired conversations, reports, plans and learning records.', flush=True)
        seed_histories(db, services, scenarios, args.as_of)
        print('Warming the local EmbeddingGemma document cache.', flush=True)
        embeddings = warm_embeddings(db, env)
        print('Checking API views, report states, citations and user isolation.', flush=True)
        result = validate(db, api, services)
        assert protected_fingerprints(db) == before, 'Existing conversation or model configuration changed'
        result.update(database=args.database, asOf=str(args.as_of), fixturePrefix=PREFIX, existingDataPreserved=True, embeddings=embeddings,
                      modelCallsBefore=health['modelCallCount'], modelCallsAfter=api.call('GET','/health')['modelCallCount'])
        output = ROOT/'backend/build/local-test-seed-result.json'
        output.parent.mkdir(exist_ok=True)
        output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
        print(json.dumps(result, ensure_ascii=False, indent=2), flush=True)
    finally:
        # Remove only sessions issued by this script; never touch the user's login.
        if api.tokens:
            db.transaction([f"DELETE FROM auth_sessions WHERE token_hash IN ({','.join(sql(t) for t in api.tokens)});"])


if __name__ == '__main__':
    main()
