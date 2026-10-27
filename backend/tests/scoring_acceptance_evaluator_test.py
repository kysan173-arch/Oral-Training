#!/usr/bin/env python3
"""Regression tests for acceptance denominators, negative controls and human review gates."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import evaluate_scoring_acceptance as evaluator


def fixtures():
    case = {'id': 'A', 'mode': 'training', 'repetitions': 1,
            'history': [{'role': 'user', 'round': 1, 'content': '本方案8900元/颗，需检查确认；附加项目另计。'}],
            'expected': {'knowledge': [{'round': 1, 'field': 'price', 'verdict': 'supported',
                                       'quote': '本方案8900元/颗，需检查确认；附加项目另计。'}],
                         'summary': {'mustMention': [{'id': 'price', 'description': '解释报价'}],
                                     'mustNotClaim': [{'id': 'booked', 'description': '已经预约'}]}}}
    fixture = {'schemaVersion': 1, 'suiteId': 'test', 'cases': [case], 'pairs': []}
    result = {'caseId': 'A', 'repetition': 1, 'mode': 'training', 'status': 'ok',
              'knowledgeAssessment': {'knowledgeChecks': [{'round': 1, 'topic': 'price',
                'originalQuote': '本方案8900元/颗，需检查确认；', 'verdict': 'supported'}]},
              'report': {'dimensionScores': {'empathy': 80}}}
    return fixture, {'suiteId': 'test', 'runMode': 'live', 'results': [result]}


class ScoringAcceptanceEvaluatorTest(unittest.TestCase):
    def test_production_topic_and_clause_quote(self):
        f, r = fixtures()
        m = evaluator.evaluate(f, r)
        self.assertEqual(m['knowledgeAccuracy'], evaluator.rate(1, 1))
        self.assertEqual(m['review']['pending'], 1)
        self.assertIsNone(m['summaryFactAccuracy']['rate'])

    def test_missing_assertion_is_failure_and_error_miss(self):
        f, r = fixtures()
        f['cases'][0]['expected']['knowledge'][0]['verdict'] = 'contradicted'
        r['results'][0]['knowledgeAssessment']['knowledgeChecks'] = []
        m = evaluator.evaluate(f, r)
        self.assertEqual(m['knowledgeAccuracy'], evaluator.rate(0, 1))
        self.assertEqual(m['errorMissRate'], evaluator.rate(1, 1))

    def test_missing_run_is_not_silently_excluded(self):
        f, r = fixtures()
        r['results'] = []
        m = evaluator.evaluate(f, r)
        self.assertEqual(m['runs']['not_run'], 1)
        self.assertEqual(m['knowledgeAccuracy']['denominator'], 1)
        self.assertGreater(m['hardFailures'], 0)

    def test_rejected_non_assertion_does_not_count_as_missed_fact(self):
        f, r = fixtures()
        f['cases'][0]['expected']['knowledge'][0]['verdict'] = 'not_applicable'
        r['results'][0]['knowledgeAssessment']['knowledgeChecks'] = []
        self.assertEqual(evaluator.evaluate(f, r)['knowledgeAccuracy'], evaluator.rate(1, 1))
        r['results'][0]['knowledgeAssessment']['knowledgeChecks'] = [
            {'round': 1, 'topic': 'price', 'originalQuote': '本方案8900元/颗，需检查确认；', 'verdict': 'contradicted'}]
        self.assertEqual(evaluator.evaluate(f, r)['falsePenaltyRate'], evaluator.rate(1, 1))

    def test_duplicate_and_unplanned_records_are_rejected(self):
        f, r = fixtures()
        r['results'].append(deepcopy(r['results'][0]))
        with self.assertRaises(ValueError):
            evaluator.evaluate(f, r)
        r['results'][-1]['caseId'] = 'unknown'
        with self.assertRaises(ValueError):
            evaluator.evaluate(f, r)

    def test_invalid_status_and_run_manifest_are_rejected(self):
        f, r = fixtures()
        r['results'][0]['status'] = 'planned'
        with self.assertRaises(ValueError):
            evaluator.evaluate(f, r)
        r['results'][0]['status'] = 'ok'
        r['expectedRunKeys'] = []
        with self.assertRaises(ValueError):
            evaluator.evaluate(f, r)

    def test_offline_does_not_invent_model_or_unscored_results(self):
        f, r = fixtures()
        f['cases'][0]['expected']['unscored'] = True
        r['runMode'], r['results'][0]['status'] = 'offline', 'offline_only'
        m = evaluator.evaluate(f, r)
        self.assertEqual(m['unscoredPending'], 1)
        self.assertIsNone(m['unscoredHandling']['rate'])
        self.assertFalse(m['scoreStability'])

    def test_unscored_report_requires_null_not_zero(self):
        f, r = fixtures()
        f['cases'][0]['expected']['unscored'] = True
        report = r['results'][0]['report']
        report.update(totalScore=None, passed=None, scoreBasis='communication_only')
        self.assertEqual(evaluator.evaluate(f, r)['unscoredHandling'], evaluator.rate(1, 1))
        report['totalScore'] = 0
        self.assertEqual(evaluator.evaluate(f, r)['unscoredHandling'], evaluator.rate(0, 1))

    def test_pair_requires_all_three_repetitions_and_reports_tie(self):
        f, r = fixtures()
        second = deepcopy(f['cases'][0]); second['id'] = 'B'
        f['cases'].append(second)
        f['pairs'] = [{'id': 'empathy', 'higherCaseId': 'A', 'lowerCaseId': 'B', 'dimension': 'empathy'}]
        r['results'].append(dict(deepcopy(r['results'][0]), caseId='B'))
        m = evaluator.evaluate(f, r)
        self.assertEqual(m['pairTies'], 1)
        self.assertEqual(m['pairConsistency'], evaluator.rate(0, 1))
        f['cases'][0]['repetitions'] = 3
        m = evaluator.evaluate(f, r)
        self.assertEqual(m['pairConsistency']['denominator'], 0)
        self.assertEqual(m['pairs'][0]['status'], 'not_evaluated')

    def test_knowledge_pairs_do_not_inflate_communication_metric(self):
        f, r = fixtures()
        second = deepcopy(f['cases'][0]); second['id'] = 'B'
        f['cases'].append(second)
        f['pairs'] = [{'id': 'knowledge', 'higherCaseId': 'A', 'lowerCaseId': 'B', 'dimension': 'knowledgeAccuracy'}]
        r['results'][0]['report']['dimensionScores']['knowledgeAccuracy'] = 100
        r['results'].append(dict(deepcopy(r['results'][0]), caseId='B'))
        r['results'][1]['report']['dimensionScores']['knowledgeAccuracy'] = 0
        m = evaluator.evaluate(f, r)
        self.assertEqual(m['knowledgePairConsistency'], evaluator.rate(1, 1))
        self.assertEqual(m['pairConsistency']['denominator'], 0)

    def complete_review(self, f, r):
        review = evaluator.review_template(f, r, 'results', 'input')
        row = review['reviews'][0]
        row.update(status='complete', reviewer='reviewer',
                   claims=[{'text': '客服说明报价。', 'supported': True, 'sourceRounds': [1]}])
        row['events'][0]['covered'] = True
        row['forbiddenClaims'][0]['occurred'] = False
        row['errors'] = dict.fromkeys(row['errors'], False)
        return review

    def test_human_claim_and_event_metrics(self):
        f, r = fixtures()
        review = self.complete_review(f, r)
        m = evaluator.evaluate(f, r, review)
        self.assertEqual(m['summaryFactAccuracy'], evaluator.rate(1, 1))
        self.assertEqual(m['summaryEventCoverage'], evaluator.rate(1, 1))
        self.assertEqual(m['review']['status'], 'complete')
        review['reviews'][0]['claims'].append({'text': '已预约', 'supported': False, 'sourceRounds': []})
        self.assertEqual(evaluator.evaluate(f, r, review)['summaryFactAccuracy'], evaluator.rate(1, 2))

    def test_incomplete_or_wrong_source_review_rejected(self):
        f, r = fixtures()
        review = self.complete_review(f, r)
        review['reviews'][0]['events'] = []
        with self.assertRaises(ValueError):
            evaluator.evaluate(f, r, review)
        review = self.complete_review(f, r)
        review['reviews'][0]['claims'][0]['sourceRounds'] = [99]
        with self.assertRaises(ValueError):
            evaluator.evaluate(f, r, review)

    def test_cli_binds_review_and_requires_review(self):
        f, r = fixtures()
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory)
            for name, data in [('input.json', f), ('results.json', r)]:
                (p / name).write_text(json.dumps(data))
            (p / 'manifest.json').write_text(json.dumps({'inputHash': evaluator.sha(p / 'input.json')}))
            command = [sys.executable, '-B', str(Path(evaluator.__file__)), str(p)]
            call = subprocess.run(command + ['--require-review'], capture_output=True)
            self.assertEqual(call.returncode, 2)
            self.assertTrue((p / 'review-template.json').exists())
            review = self.complete_review(f, r)
            (p / 'review.json').write_text(json.dumps(review))
            call = subprocess.run(command + ['--review', str(p / 'review.json'), '--output', str(p / 'other.json')], capture_output=True)
            self.assertEqual(call.returncode, 2)
            self.assertFalse((p / 'other.json').exists())


if __name__ == '__main__':
    unittest.main()
