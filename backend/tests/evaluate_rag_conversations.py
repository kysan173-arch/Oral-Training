"""Deterministic behavioral assertions; report retrieval and final-answer errors separately."""
import argparse
import json
import statistics
import re
from pathlib import Path



def initialization_errors(r):
    errors=[]
    if 'error' in r: return ['generation: '+r['error']]
    source=r.get('rawOutput',{})
    allowed={'displayName':['李女士','王先生','陈女士','张先生'],
             'ageRange':['20-29','30-39','40-49','50-59','60-69'],
             'concern':['价格','疼痛','时间','服务流程','恢复安排'],
             'budget':['3000','5000','8000','12000','20000'],
             'emotion':['平静','犹豫','焦虑']}
    if not isinstance(source,dict) or any(source.get(k) not in v for k,v in allowed.items()):
        errors.append('generation: invalid persona selection')
    result=r.get('result',{})
    if not result.get('opening') or result.get('patientState',{}).get('revealedInformation') != []:
        errors.append('patient: invalid private initial state')
    if any(s in result.get('opening','') for s in ['预算','比较','另外两家','系统提示']):
        errors.append('patient: opening discloses private persona')
    return errors


def patient_contract_errors(raw):
    if (not isinstance(raw,dict) or not isinstance(raw.get('reply'),str) or not raw['reply'] or
        raw.get('intent') not in ['clarify','price','pain','time','process','followup','finish'] or
        not isinstance(raw.get('evidenceIds'),list) or not all(isinstance(x,str) for x in raw['evidenceIds'])):
        return ['generation: invalid patient output contract']
    return []


def patient_behavior_errors(t,r):
    errors=[]
    result=r.get('result',{}); text=result.get('reply','')
    state=result.get('patientState',{}); before=r.get('beforeState',{})
    allowed=r.get('modelView',{}).get('allowedInformation',{})
    revealed=state.get('revealedInformation',[])
    budget=r.get('persona',{}).get('budget','')
    if t.get('hideBudget') and ('budget' in allowed or 'budget' in revealed or (budget and budget in text)):
        errors.append('patient: budget leaked before permission')
    if t.get('hideCompetitor') and ('competitor' in allowed or 'competitor' in revealed or
                                  any(x in text for x in ['别家','其他诊所','两家'])):
        errors.append('patient: competitor leaked before permission')
    if t.get('revealBudget') and (not budget or budget not in text or 'budget' not in revealed):
        errors.append('patient: actual immutable budget was not disclosed when asked')
    if t.get('revealCompetitor') and 'competitor' not in revealed:
        errors.append('patient: competitor disclosure not tracked')
    if not t.get('revealBudget') and re.search(r'[0-9]',text):
        errors.append('patient: numeric clinic claim or unrequested budget')
    if 'trustDelta' in t and state.get('trustLevel') != before.get('trustLevel',50)+t['trustDelta']:
        errors.append('patient: incorrect trust change')
    if 'riskTriggered' in t and state.get('riskTriggered') != t['riskTriggered']:
        errors.append('patient: incorrect persistent risk state')
    if t.get('noPriceObjection') and 'price_verification' in state.get('triggeredObjections',[]):
        errors.append('patient: duration or bare number mistaken for quote')
    if 'shouldEnd' in t and result.get('shouldEnd') != t['shouldEnd']:
        errors.append('patient: incorrect session ending')
    if text.count('？')+text.count('?') > t.get('questionCount',1):
        errors.append('patient: too many questions')
    if not 0 <= state.get('trustLevel',-1) <= 100:
        errors.append('patient: missing or invalid trust')
    evidence=r.get('evidence',{})
    known={e['evidenceId'] for e in evidence.get('facts',[])+evidence.get('passages',[])}
    if any(c.get('evidenceId') not in known for c in result.get('citations',[])):
        errors.append('patient: citation outside current evidence')
    for marker in ['系统提示','资料原文','适用范围','作为AI','作为一个AI','建议您服','建议你服']:
        if marker in text: errors.append('patient: role or internal detail leaked')
    return errors


def evaluate(suite, records, cohort='development'):
    expected = {}
    for c in suite['conversations']:
        if cohort != 'all' and c['cohort'] != cohort:
            continue
        role = c.get('role', 'customer')
        for t in c['turns']:
            expected[c['id'],t['id']] = dict(t,role=role)
        if role == 'patient':
            expected[c['id'],'__initialization__'] = {'role':role,'question':'选择虚构患者画像'}
    seen = set()
    rows = []
    for r in records:
        identity = r['conversationId'], r['turnId']
        if identity not in expected or identity in seen:
            raise ValueError('Unexpected or duplicate result: ' + str(identity))
        seen.add(identity)
        t = expected[identity]
        errors = []
        result = r.get('result', {})
        text = result.get('reply', '')
        raw = r.get('rawOutput', {})
        raw_reply = raw.get('reply', '') if isinstance(raw, dict) else ''
        role = t.get('role','customer')
        if identity[1] == '__initialization__':
            errors = initialization_errors(r)
            rows.append({'conversationId':identity[0],'turnId':identity[1],'role':role,
                         'reply':result.get('opening',''),'errors':errors,'pass':not errors})
            continue
        if 'error' in r:
            errors.append('generation: ' + r['error'])
        elif role == 'patient':
            errors.extend(patient_contract_errors(raw))
        elif (not isinstance(raw, dict) or not isinstance(raw.get('reply'), str) or
              not raw.get('reply') or raw.get('replyKind') not in {'answer','conversation','unavailable'} or
              not isinstance(raw.get('evidenceIds'), list) or
              not all(isinstance(e, str) for e in raw.get('evidenceIds', [])) or
              not isinstance(raw.get('shouldEnd'), bool) or
              raw.get('unavailableTopic') not in {'general','appointment','price','duration','promotion','medical',''}):
            errors.append('generation: invalid output contract')
        elif ((raw['replyKind'] == 'answer' and not raw['evidenceIds']) or
              (raw['replyKind'] != 'answer' and raw['evidenceIds'])):
            errors.append('generation: reply kind and citations disagree')
        if not text:
            errors.append('answer: empty')
        if role == 'patient':
            errors.extend(patient_behavior_errors(t,r))
        if t.get('expectedTopics') and not set(t['expectedTopics']) & set(r.get('retrievedTopics', [])):
            errors.append('retrieval: missing expected topic')
        if any(f not in r.get('retrievedFields', []) for f in t.get('expectedFields', [])):
            errors.append('retrieval: missing structured field')
        for group in t.get('requiredGroups', []):
            if not any(term in text for term in group):
                errors.append('answer: missing concept ' + '/'.join(group))
        for term in t.get('forbidden', []):
            if term in text:
                errors.append('answer: forbidden ' + term)
        if t.get('requireCitation') and not result.get('citations'):
            errors.append('answer: no citation for supported fact')
        if t.get('requireCitation') and result.get('citations'):
            selected = {c.get('evidenceId') for c in result['citations']}
            passages = r.get('evidence', {}).get('passages', [])
            facts = r.get('evidence', {}).get('facts', [])
            known = {p['evidenceId'] for p in passages + facts}
            cited_topics = {topic for p, topic in zip(passages, r.get('retrievedTopics', []))
                            if p['evidenceId'] in selected}
            cited_fields = {f['field'] for f in facts if f['evidenceId'] in selected}
            if not selected <= known:
                errors.append('answer: citation not in this turn evidence')
            if t.get('expectedTopics') and not set(t['expectedTopics']) & cited_topics:
                errors.append('answer: cited source is unrelated to expected topic')
            if t.get('expectedFields') and not set(t['expectedFields']) <= cited_fields:
                errors.append('answer: missing citation to structured answer field')
        if len(result.get('citations', [])) > t.get('maxCitations', 3):
            errors.append('answer: too many/unwanted citations')
        if t.get('allowedStatus') and result.get('answerStatus') not in t['allowedStatus']:
            errors.append('answer: unexpected status ' + result.get('answerStatus', 'missing'))
        if t.get('replyKind') and result.get('replyKind') != t['replyKind']:
            errors.append('answer: wrong conversational intent')
        if len(text) > t.get('maxCharacters', 180):
            errors.append('answer: too long')
        if any(term in text for term in ['<think>', '```', '资料原文：', 'evidenceId']):
            errors.append('answer: internal format leaked')
        rows.append({'conversationId': identity[0], 'turnId': identity[1], 'role':role, 'question':t['question'],
                     'reply': text, 'rawReply':raw_reply,
                     'guardChanged': text != raw_reply,
                     'retrievedTopics':r.get('retrievedTopics',[]), 'errors': errors, 'pass': not errors})
    for identity in expected.keys() - seen:
        rows.append({'conversationId':identity[0],'turnId':identity[1], 'role':expected[identity]['role'], 'errors':['missing result'],'pass':False})
    return {'evaluationVersion':'dual-role-behavior-v1','suiteVersion':suite.get('version','test'),'total':len(expected),'passed':sum(r['pass'] for r in rows),
            'generationFailures':sum(any(e.startswith('generation:') for e in r['errors']) for r in rows),
            'retrievalFailures':sum(any(e.startswith('retrieval:') for e in r['errors']) for r in rows),
            'answerFailures':sum(any(not e.startswith('retrieval:') for e in r['errors']) for r in rows),
            'guardChanges':sum(r.get('guardChanged',False) for r in rows),
            'medianElapsedMs':statistics.median([r.get('elapsedMs',0) for r in records]) if records else 0,
            'roles':{role:{'total':sum(x.get('role')==role for x in rows),
                'passed':sum(x.get('role')==role and x['pass'] for x in rows)} for role in ['customer','patient']},
            'totalTokens':sum(r.get('usage',{}).get('total_tokens',0) for r in records), 'turns':rows}


def main():
    p=argparse.ArgumentParser()
    p.add_argument('results',type=Path)
    p.add_argument('--suite',type=Path,default=Path(__file__).parent/'fixtures/rag_conversations.json')
    p.add_argument('--cohort',choices=['development','holdout','all'],default='development')
    args=p.parse_args()
    summary=evaluate(json.loads(args.suite.read_text(encoding='utf-8')),[json.loads(x) for x in args.results.read_text(encoding='utf-8').splitlines()],args.cohort)
    args.results.with_suffix('.summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n', encoding='utf-8')
    print(f"{summary['passed']}/{summary['total']} passed; retrieval failures {summary['retrievalFailures']}; answer failures {summary['answerFailures']}")
    for row in summary['turns']:
        if not row['pass']:
            print(row['conversationId']+'/'+row['turnId'], '; '.join(row['errors']), '\n ', row.get('reply',''))
    return 0 if summary['passed']==summary['total'] else 1

if __name__=='__main__':
    raise SystemExit(main())
