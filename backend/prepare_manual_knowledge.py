#!/usr/bin/env python3
"""Complete the existing local demo knowledge catalog through audited publication APIs."""
import argparse
from datetime import datetime
import json
from pathlib import Path
import time
from zoneinfo import ZoneInfo

from seed_local_test_data import (ADMIN, Api, Database, ROOT, digest, insert,
                                 load_env, service_specs, sql, unknown, warm_embeddings)

PREFIX = 'manual-test-v1-'
LEARNER = PREFIX + 'learner'
CATALOG = ROOT / 'docs/rag-data/manual-test-knowledge.json'
RESULT = ROOT / 'backend/build/manual-knowledge-result.json'


class PacedApi(Api):
    last_call = 0.0

    def call(self, *args, **kwargs):
        time.sleep(max(0, 0.65 - (time.monotonic() - self.last_call)))
        self.last_call = time.monotonic()
        return super().call(*args, **kwargs)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def same_service(a, b):
    if not isinstance(a, dict) or not isinstance(b, dict):
        return a == b
    return dict(a, scenarioIds=sorted(a.get('scenarioIds', []))) == dict(b, scenarioIds=sorted(b.get('scenarioIds', [])))


def metadata(item):
    value = dict(origin='synthetic', verification='unverified', trainingScope='demo',
                 applicability=item.get('applicability', '仅用于本地虚构门诊客服手动测试，按所选服务解释。'),
                 aliases=item.get('aliases', []), sourceTitle='', sourceUrl=None, sourceLocator='',
                 fixtureKey=PREFIX + item['key'])
    value.update(item.get('dates', {}))
    return value


def boundaries():
    output = []
    for key, label, body, dates, state in [
        ('boundary-current', '当前有效', '资料核验口令为青松。此条为当前有效的发布资料，可供新会话检索。', {}, 'published'),
        ('boundary-expired', '已过期', '过期核验口令为旧橡。此条超过有效期，不应进入当前新会话的知识快照。', {'effectiveFrom':'2000-01-01','effectiveUntil':'2000-12-31'}, 'published'),
        ('boundary-future', '未生效', '未来核验口令为新柳。此条尚未到生效日，不应进入当前新会话的知识快照。', {'effectiveFrom':'2099-01-01'}, 'published'),
        ('boundary-draft', '未发布草稿', '草稿核验口令为草榆。此条尚未发布，只供管理草稿页面查看。', {}, 'draft'),
        ('boundary-archived', '已下架', '下架核验口令为停杉。此条曾发布但现已下架，不应进入当前新会话。', {}, 'archived'),
    ]:
        output.append(dict(key=key, service='boundary', topic='manual-'+key, title='[手测边界] '+label+'资料',
                           body=body, dates=dates, state=state,
                           aliases=['资料核验口令'], questions=[]))
    for suffix, number in [('a', '1'), ('b', '3')]:
        output.append(dict(key='boundary-conflict-'+suffix, service='boundary', topic='manual-boundary-conflict',
                           title='[手测边界] 资料核验反馈时限 '+suffix.upper(),
                           body='资料核验反馈时限为'+number+'个工作日。',
                           applicability='仅用于知识边界服务的冲突识别，同一资料核验事项。',
                           aliases=['资料核验反馈时限'], questions=[], state='archived'))
        output.append(dict(key='isolated-conflict-'+suffix, service='conflict', topic='manual-isolated-conflict',
                           title='[手测冲突] 橙云资料核验反馈时限 '+suffix.upper(),
                           body='橙云资料核验反馈时限为'+number+'个工作日。',
                           applicability='仅用于独立知识冲突服务，同一橙云资料核验事项。',
                           aliases=['橙云资料核验反馈时限'], questions=[]))
    output.append(dict(key='boundary-version', service='boundary', topic='manual-boundary-version',
                       title='[手测边界] 版本快照联系窗口', body='版本快照联系窗口是蓝桥窗口。',
                       aliases=['版本快照联系窗口'], questions=[]))
    return output


def preserved_fingerprints(db):
    conditions = {
        'users': f'id <> {sql(LEARNER)}',
        'sessions': 'TRUE', 'messages': 'TRUE', 'evaluations': 'TRUE',
        'roleplay_sessions': f'user_id <> {sql(LEARNER)}',
        'roleplay_messages': f'session_id NOT IN (SELECT id FROM roleplay_sessions WHERE user_id={sql(LEARNER)})',
        'roleplay_summaries': f'session_id NOT IN (SELECT id FROM roleplay_sessions WHERE user_id={sql(LEARNER)})',
        'training_contexts': f'session_id NOT IN (SELECT id FROM roleplay_sessions WHERE user_id={sql(LEARNER)})',
        'user_model_gateway_settings': 'TRUE', 'model_gateway_settings': 'TRUE',
    }
    return {table: db.run(f"SELECT md5(COALESCE(string_agg(to_jsonb(t)::text,'|' ORDER BY to_jsonb(t)::text),'')) FROM {table} t WHERE {condition}", True)
            for table, condition in conditions.items()}


class CatalogWriter:
    def __init__(self, db, api, token):
        self.db, self.api, self.token = db, api, token
        self.changed = []

    def publish(self, kind, entity, draft):
        self.api.call('POST', f'/admin/{kind}/{entity}/publish',
                      dict(draftVersion=draft['draftVersion'],
                           idempotencyKey=PREFIX+kind+'-'+entity+'-'+str(draft['draftVersion'])), self.token)
        self.changed.append(kind+':'+entity)

    def service(self, key, payload, previous=None):
        rows = self.db.rows(f"SELECT s.id,s.status,s.current_revision_id,r.payload FROM clinic_services s LEFT JOIN service_revisions r ON r.id=s.current_revision_id WHERE s.created_by={sql(ADMIN)} AND s.name={sql(payload['name'])}")
        require(len(rows)<=1, 'Duplicate fixture service: '+key)
        if not rows:
            require(previous is None, 'Run the base local test-data initializer first: '+key)
            created = self.api.call('POST','/admin/services',{'payload':payload},self.token,201)
            rows = [dict(id=created['id'],status='active',current_revision_id=None,payload=None)]
        row = rows[0]; entity = row['id']
        require(row['status']=='active', 'Preserving archived service; inspect before republishing: '+entity)
        draft = self.api.call('GET', f'/admin/services/{entity}/draft',token=self.token)
        if same_service(row['payload'], payload):
            require(same_service(draft['payload'], payload), 'Preserving unexpected service draft: '+entity)
        if not same_service(row['payload'], payload):
            require(same_service(draft['payload'],payload) or (previous is not None and same_service(draft['payload'],previous) and same_service(row['payload'],previous)),
                    'Preserving unexpected service edits: '+entity)
            if not same_service(draft['payload'], payload):
                draft = self.api.call('PUT',f'/admin/services/{entity}/draft',dict(draftVersion=draft['draftVersion'],payload=payload),self.token)
            self.publish('services',entity,draft)
        return self.db.rows(f"SELECT s.id,s.current_revision_id AS revision,r.payload FROM clinic_services s JOIN service_revisions r ON r.id=s.current_revision_id WHERE s.id={sql(entity)}")[0]

    def knowledge(self, item, services):
        sid = '' if item['service']=='general' else services[item['service']]['id']
        payload = dict(topic=item['topic'], scope='service' if sid else 'general', serviceId=sid,
                       title=item['title'], body=item['body'], metadata=metadata(item))
        fixture_key = sql(PREFIX+item['key'])
        rows = self.db.rows(f"SELECT k.id,k.status,k.current_revision_id,r.title,r.body,r.metadata,d.title AS draft_title,d.body AS draft_body,d.metadata AS draft_metadata FROM knowledge_entries k JOIN knowledge_drafts d ON d.entry_id=k.id LEFT JOIN knowledge_revisions r ON r.id=k.current_revision_id WHERE k.created_by={sql(ADMIN)} AND (d.metadata->>'fixtureKey'={fixture_key} OR r.metadata->>'fixtureKey'={fixture_key})")
        require(len(rows)<=1, 'Duplicate fixture key: '+item['key'])
        if not rows:
            created = self.api.call('POST','/admin/knowledge',payload,self.token,201)
            rows = [dict(id=created['id'],status='active',current_revision_id=None)]
        row=rows[0];entity=row['id'];state=item.get('state','published')
        if row['current_revision_id']:
            expected_body = [item['body']]
            if item['key']=='boundary-version': expected_body.append('版本快照联系窗口是绿洲窗口。')
            require(row['title']==item['title'] and row['body'] in expected_body and row['metadata']==payload['metadata'],
                    'Preserving unexpected published fixture edits: '+entity)
            expected_draft_bodies = [row['body']]
            if item['key']=='boundary-version' and row['body']==item['body']:
                expected_draft_bodies.append('版本快照联系窗口是绿洲窗口。')
            require(row['draft_title']==item['title'] and row['draft_body'] in expected_draft_bodies and row['draft_metadata']==payload['metadata'],
                    'Preserving unexpected knowledge draft: '+entity)
            require(row['status']=='active' or state=='archived','Preserving manually archived knowledge: '+entity)
        else:
            draft=self.api.call('GET',f'/admin/knowledge/{entity}/draft',token=self.token)
            require(all(draft[k]==payload[k] for k in ['title','body','metadata']), 'Preserving unexpected knowledge draft: '+entity)
            if state!='draft': self.publish('knowledge',entity,draft)
        if state=='archived' and row['status']!='archived':
            self.api.call('POST',f'/admin/knowledge/{entity}/archive',{},self.token)
        return self.db.rows(f"SELECT k.id,k.current_revision_id AS revision,k.status,r.body FROM knowledge_entries k LEFT JOIN knowledge_revisions r ON r.id=k.current_revision_id WHERE k.id={sql(entity)}")[0]


def service_plan(scenarios):
    original = {s['key']:s['payload'] for s in service_specs(scenarios)}
    mapping = {
        'implant':['implant-basic','price-comparison','post-treatment-discomfort','guarantee-demand'],
        'ortho':['orthodontic-basic','orthodontic-option','price-comparison','guarantee-demand'],
        'cleaning':['price-comparison','guarantee-demand'],
    }
    planned = {key:dict(original[key],scenarioIds=ids) for key,ids in mapping.items()}
    base = dict(dataOrigin='synthetic', includedItems=[], excludedItems=[], professionalTopics=[],
                price=unknown('此服务用于流程与知识边界测试，没有可用诊疗报价。'),
                visitDuration=unknown(), treatmentDuration=unknown(), followupInterval=unknown(),
                appointment=unknown('系统没有实时号源，需通过实际门诊渠道确认。'))
    planned['reception']=dict(base,name='[手测] 门诊咨询与投诉受理',category='门诊服务流程',scenarioIds=[
        'complaint-about-doctor','refund-demand','treatment-expectation-gap','waiting-too-long'])
    planned['boundary']=dict(base,name='[手测] 知识发布与时效边界',category='知识边界测试',scenarioIds=['price-comparison'])
    planned['conflict']=dict(base,name='[手测] 知识冲突演练',category='知识冲突测试',scenarioIds=['price-comparison'])
    for key,dates in [('expired',dict(validUntil='2000-12-31')),('future',dict(validFrom='2099-01-01'))]:
        planned[key]=dict(base,name='[手测] '+('过期' if key=='expired' else '未生效')+'报价服务',category='报价时效测试',
                          scenarioIds=['price-comparison'],price=dict(status='known',type='fixed',amountMinor=10000,currency='CNY',unit='per_visit',
                          conditions='仅用于报价有效期筛选的虚构金额。',**dates))
    return original,planned


def snapshot(db, api, token, service, key):
    client=PREFIX+'snapshot-'+key
    value=api.call('POST','/roleplay/sessions',dict(serviceId=service['id'],scenarioId=service['payload']['scenarioIds'][0],clientSessionId=client),token,201)
    sid=value['session']['id']
    if value['session']['status']=='in_progress':
        api.call('POST',f'/roleplay/sessions/{sid}/abandon',{},token,expected=202)
    return db.rows(f"SELECT session_id,service_revision_id,manifest,manifest_hash,knowledge_as_of FROM training_contexts WHERE session_type='roleplay' AND session_id={sql(sid)}")[0]


def publish_version_fixture(api, writer, version, item):
    if version['body']!='版本快照联系窗口是蓝桥窗口。':
        return
    path=f"/admin/knowledge/{version['id']}/draft"
    draft=api.call('GET',path,token=writer.token)
    target='版本快照联系窗口是绿洲窗口。'
    require(draft['title']==item['title'] and draft['metadata']==metadata(item) and draft['body'] in [version['body'],target],
            'Preserving edited version-test draft')
    if draft['body']!=target:
        draft=api.call('PUT',path,dict(draftVersion=draft['draftVersion'],title=draft['title'],
                       body=target,metadata=draft['metadata']),writer.token)
    writer.publish('knowledge',version['id'],draft)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--database',required=True)
    parser.add_argument('--env-file',type=Path,default=ROOT/'backend/backend.env')
    parser.add_argument('--apply',action='store_true')
    args=parser.parse_args()
    env=load_env(args.env_file)
    require(env.get('PRODUCTION','').lower() in ('false','0','no','off') and env.get('AUTH_MODE')=='demo','Local non-production demo configuration required')
    db=Database(env,args.database);api=PacedApi(int(env.get('PORT','8080')))
    health=api.call('GET','/health')
    require(health['ready'] and not health['production'] and health['authMode']=='demo','Healthy demo backend required')
    require(db.rows("SELECT name FROM schema_migrations WHERE name='036_embeddinggemma_cache.sql'"),'Migration 036 required')
    require(db.rows(f"SELECT id FROM users WHERE id={sql(ADMIN)} AND role='admin' AND is_demo"),'Initialize base local test data first')
    scenarios=db.rows('SELECT id,name,is_active,is_template FROM scenarios ORDER BY sort_order')
    scenario_ids=[s['id'] for s in scenarios]
    corpus=json.loads(CATALOG.read_text())
    items=corpus['knowledge']+boundaries()
    require(len({i['key'] for i in items})==len(items),'Fixture keys must be unique')
    original,planned=service_plan(scenario_ids)
    active={s['id'] for s in scenarios if s['is_active'] and not s['is_template']}
    covered={sid for p in planned.values() for sid in p['scenarioIds']}
    require(active<=covered and covered<=active,'All visible scenarios need valid coverage without inactive templates')
    print(json.dumps(dict(database=args.database,apply=args.apply,visibleScenarios=len(active),additionalKnowledge=len(items)),ensure_ascii=False),flush=True)
    if not args.apply:return
    before=preserved_fingerprints(db)
    RESULT.parent.mkdir(exist_ok=True)
    backup=RESULT.parent/('manual-knowledge-before-'+datetime.now(ZoneInfo('Asia/Shanghai')).strftime('%Y%m%d-%H%M%S')+'.json')
    backup.write_text(json.dumps(dict(services=db.rows('SELECT to_jsonb(s) AS row FROM clinic_services s'),
                                    drafts=db.rows('SELECT to_jsonb(d) AS row FROM service_drafts d'),fingerprints=before),ensure_ascii=False,indent=2)+'\n')
    user=db.rows(f'SELECT role,is_demo FROM users WHERE id={sql(LEARNER)}')
    require(not user or user[0]==dict(role='learner',is_demo=True),'Fixture learner identity conflict')
    db.transaction([insert('users',dict(id=LEARNER,display_name='Test 手测·知识版本核验',role='learner',status='active',is_demo=True))])
    try:
        writer=CatalogWriter(db,api,api.login(ADMIN))
        print('Publishing compatible service mappings and dedicated boundary services.',flush=True)
        services={key:writer.service(key,payload,original.get(key)) for key,payload in planned.items()}
        print('Publishing additional knowledge through versioned admin APIs.',flush=True)
        entries={}
        for n,item in enumerate(items,1):
            entries[item['key']]=writer.knowledge(item,services)
            if n%8==0:print(f'Knowledge prepared: {n}/{len(items)}',flush=True)
        learner=api.login(LEARNER)
        old=snapshot(db,api,learner,services['boundary'],'version-old')
        version=entries['boundary-version']
        publish_version_fixture(api,writer,version,next(i for i in items if i['key']=='boundary-version'))
        print('Warming published document vectors and verifying training snapshots.',flush=True)
        embeddings=warm_embeddings(db,env)
        snapshots={}
        for key in ['implant','ortho','cleaning','reception','boundary','conflict']:
            current=db.rows(f"SELECT k.current_revision_id FROM knowledge_entries k JOIN knowledge_revisions r ON r.id=k.current_revision_id WHERE k.status='active' AND r.metadata->>'trainingScope'='demo' AND (k.scope='general' OR k.service_id={sql(services[key]['id'])}) AND COALESCE(NULLIF(r.metadata->>'effectiveFrom',''),'0001-01-01') <= to_char(now() AT TIME ZONE 'Asia/Shanghai','YYYY-MM-DD') AND COALESCE(NULLIF(r.metadata->>'effectiveUntil',''),'9999-12-31') >= to_char(now() AT TIME ZONE 'Asia/Shanghai','YYYY-MM-DD') ORDER BY k.current_revision_id")
            signature=digest([services[key]['revision'],current])[:12]
            snapshots[key]=snapshot(db,api,learner,services[key],key+'-current-'+signature)
        current_version=db.rows(f"SELECT current_revision_id FROM knowledge_entries WHERE id={sql(version['id'])}")[0]['current_revision_id']
        require(current_version in snapshots['boundary']['manifest'] and current_version not in old['manifest'],'Version snapshots must stay distinct')
        boundary_manifest=snapshots['boundary']['manifest']
        require(entries['boundary-current']['revision'] in boundary_manifest,'Current knowledge missing')
        for key in ['boundary-expired','boundary-future','boundary-draft','boundary-archived','boundary-conflict-a','boundary-conflict-b']:
            require(entries[key]['revision'] not in boundary_manifest,'Excluded knowledge leaked: '+key)
        for key,ctx in snapshots.items():
            revisions=','.join(sql(v) for v in ctx['manifest'])
            leak=db.rows(f"SELECT k.id FROM knowledge_revisions r JOIN knowledge_entries k ON k.id=r.entry_id WHERE r.id IN ({revisions}) AND k.scope='service' AND k.service_id <> {sql(services[key]['id'])}")
            require(not leak,'Cross-service manifest leak: '+key)
        available=api.call('GET','/services',token=learner)['items']
        available_ids={s['id'] for s in available}
        for key in ['expired','future']:require(services[key]['id'] not in available_ids,'Invalid quotation exposed')
        counts={}
        for key in ['implant','ortho','cleaning','reception','boundary','conflict']:
            expected=set(services[key]['payload']['scenarioIds'])
            for endpoint in ['/scenarios','/roleplay/scenarios']:
                actual=api.call('GET',endpoint+'?serviceId='+services[key]['id'],token=learner)['items']
                require(expected<={s['id'] for s in actual},'Missing scenarios in '+endpoint+':'+key)
            counts[key]=len(snapshots[key]['manifest'])
        require(preserved_fingerprints(db)==before,'Existing user data changed during import; inspect concurrent activity')
        result=dict(database=args.database,fixturePrefix=PREFIX,changed=writer.changed,services=services,entries=entries,
                    snapshots=snapshots,oldSnapshot=old,manifestCounts=counts,visibleScenarios=sorted(active),
                    embeddings=embeddings,existingDataPreserved=True,sourceBackup=str(backup),
                    modelCallsBefore=health['modelCallCount'],modelCallsAfter=api.call('GET','/health')['modelCallCount'],
                    totals=db.rows("SELECT (SELECT count(*) FROM clinic_services) AS services,(SELECT count(*) FROM knowledge_entries) AS knowledge,(SELECT count(*) FROM knowledge_revisions) AS revisions,(SELECT count(*) FROM knowledge_chunks) AS chunks,(SELECT count(*) FROM knowledge_chunk_embeddings) AS embeddings")[0])
        RESULT.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
        print(json.dumps({k:result[k] for k in ['database','totals','manifestCounts','existingDataPreserved','modelCallsBefore','modelCallsAfter']},ensure_ascii=False,indent=2),flush=True)
        print('Result:',RESULT,flush=True)
    finally:
        if api.tokens:db.transaction([f"DELETE FROM auth_sessions WHERE token_hash IN ({','.join(sql(t) for t in api.tokens)});"])


if __name__=='__main__':
    main()
