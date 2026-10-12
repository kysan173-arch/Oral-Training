import copy
import unittest
from evaluate_rag_conversations import evaluate, patient_behavior_errors, initialization_errors

class ConversationEvaluatorTest(unittest.TestCase):
    def setUp(self):
        self.suite={'conversations':[{'id':'cost','cohort':'development','turns':[{
            'id':'price','question':'示例总价？','requiredGroups':[['8900']],
            'expectedTopics':['cost'],'requireCitation':True,'allowedStatus':['answered']}]}]}
        self.record={'conversationId':'cost','turnId':'price','retrievedTopics':['cost'],
                     'evidence':{'passages':[{'evidenceId':'E1'}]},
                     'rawOutput':{'reply':'示例总价8900元。','replyKind':'answer','evidenceIds':['E1'],
                                  'shouldEnd':False,'unavailableTopic':'general'},'result':{'reply':'示例总价8900元。',
                     'answerStatus':'answered','citations':[{'evidenceId':'E1'}]}}

    def test_supported_price(self):
        self.assertEqual(evaluate(self.suite,[self.record])['passed'],1)

    def test_fallback_is_not_success(self):
        record=copy.deepcopy(self.record)
        record['result']={'reply':'目前无法确认。','answerStatus':'unknown','citations':[]}
        report=evaluate(self.suite,[record])
        self.assertEqual(report['passed'],0)
        self.assertTrue(report['turns'][0]['guardChanged'])
        self.assertEqual(report['answerFailures'],1)

    def test_retrieval_separate_from_answer(self):
        self.suite['conversations'][0]['turns'][0]['expectedFields']=['price']
        self.record['evidence']['facts']=[{'evidenceId':'E2','field':'price'}]
        self.record['result']['citations'].append({'evidenceId':'E2'})
        report=evaluate(self.suite,[self.record])
        self.assertEqual(report['retrievalFailures'],1)
        self.assertEqual(report['answerFailures'],0)

    def test_unrelated_or_forged_citation_cannot_pass(self):
        self.record['result']['citations']=[{'evidenceId':'E999'}]
        self.assertEqual(evaluate(self.suite,[self.record])['passed'],0)

    def test_plain_text_generation_cannot_pass(self):
        self.record['rawOutput']={}
        report=evaluate(self.suite,[self.record])
        self.assertEqual(report['generationFailures'],1)
        self.assertEqual(report['passed'],0)

    def test_non_object_json_is_reported_without_crashing(self):
        self.record['rawOutput']=['not an object']
        self.assertEqual(evaluate(self.suite,[self.record])['generationFailures'],1)

    def test_missing_duplicate_rows_cannot_pass(self):
        self.assertEqual(evaluate(self.suite,[])['passed'],0)
        with self.assertRaises(ValueError): evaluate(self.suite,[self.record,self.record])

class PatientEvaluatorTest(unittest.TestCase):
    def setUp(self):
        self.record={'persona':{'budget':'8000'},'modelView':{'allowedInformation':{}},
            'beforeState':{'trustLevel':50},'evidence':{'facts':[],'passages':[]},
            'result':{'reply':'我想先了解流程。','shouldEnd':False,'citations':[],
                'patientState':{'trustLevel':50,'riskTriggered':False,'revealedInformation':[],
                                'triggeredObjections':[]}}}

    def test_private_prompt_leak_is_a_failure_even_without_spoken_leak(self):
        self.assertEqual(patient_behavior_errors({'hideBudget':True},self.record),[])
        self.record['modelView']['allowedInformation']['budget']='8000'
        self.assertTrue(patient_behavior_errors({'hideBudget':True},self.record))

    def test_trust_and_ending_are_checked_independently_of_fluent_prose(self):
        self.record['result']['patientState']['trustLevel']=40
        self.record['result']['shouldEnd']=True
        errors=patient_behavior_errors({'trustDelta':0,'shouldEnd':False},self.record)
        self.assertEqual(len(errors),2)

    def test_initialization_must_use_bounded_persona(self):
        record={'rawOutput':{'displayName':'李女士','ageRange':'30-39','concern':'价格',
                    'budget':'8000','emotion':'平静'},
                'result':{'opening':'你好，想问一下费用。','patientState':{'revealedInformation':[]}}}
        self.assertEqual(initialization_errors(record),[])
        record['rawOutput']['budget']='999999'
        self.assertTrue(initialization_errors(record))

if __name__=='__main__': unittest.main()
