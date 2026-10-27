#!/usr/bin/env python3
"""Evaluate frozen scoring cases. Semantic summary metrics require explicit human review.
No model calls, database connections or keyword-based pretend semantic accuracy.
"""
import argparse
import hashlib
import json
from pathlib import Path
import statistics
import sys


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rate(numerator, denominator):
    return {'numerator': numerator, 'denominator': denominator,
            'rate': numerator / denominator if denominator else None}


def key(record):
    return (record['caseId'], record['repetition'], record['mode'])


def planned_keys(fixture, results):
    expected = [(c['id'], n, c['mode']) for c in fixture['cases']
                for n in range(1, c.get('repetitions', 1) + 1)]
    if 'expectedRunKeys' in results and [key(x) for x in results['expectedRunKeys']] != expected:
        raise ValueError('Run keys do not match the frozen input')
    return expected


def review_template(fixture, results, results_hash, input_hash):
    by_id = {case['id']: case for case in fixture['cases']}
    reviews = []
    for result in results['results']:
        if result.get('status') != 'ok':
            continue
        case = by_id[result['caseId']]
        expected = case['expected'].get('summary', {})
        reviews.append({
            'caseId': result['caseId'], 'repetition': result['repetition'], 'mode': result['mode'],
            'status': 'pending', 'reviewer': '',
            'claims': [],
            'claimInstructions': '拆出最终总结中的全部事实性断言；每项填text、supported(true/false)、sourceRounds。不得只挑正确句。',
            'events': [{'id': event['id'], 'description': event['description'], 'covered': None}
                       for event in expected.get('mustMention', [])],
            'forbiddenClaims': [{'id': event['id'], 'description': event['description'], 'occurred': None}
                                for event in expected.get('mustNotClaim', [])],
            'errors': {'misattribution': None, 'correctionState': None, 'fabricatedResolution': None,
                       'falseViolation': None, 'missedViolation': None},
            'notes': ''
        })
    return {'schemaVersion': 1, 'resultsHash': results_hash, 'inputHash': input_hash,
            'instructions': '先读固定对话和预期，再逐句审阅最终report/summary；全部标注后status改complete。争议保持pending。',
            'reviews': reviews}


def evaluate(fixture, results, review=None):
    if fixture['suiteId'] != results['suiteId']:
        raise ValueError('Suite mismatch')
    cases = {case['id']: case for case in fixture['cases']}
    if len(cases) != len(fixture['cases']):
        raise ValueError('Duplicate case IDs')
    planned = planned_keys(fixture, results)
    actual = {}
    for result in results['results']:
        run_key = key(result)
        if run_key in actual or run_key not in planned:
            raise ValueError('Duplicate or unplanned result')
        actual[run_key] = result
    assessments = []
    assertion_count = matched_count = wrong_count = missed_count = safe_count = penalty_count = 0
    unscored_total = unscored_correct = unscored_pending = 0
    runs = {'planned': len(planned), 'ok': 0, 'offline_only': 0, 'error': 0, 'not_run': 0}
    scores = {}
    unexpected_checks = []
    for run_key in planned:
        case = cases[run_key[0]]
        result = actual.get(run_key, {})
        status = result.get('status', 'not_run')
        if status not in ('ok', 'offline_only', 'error', 'not_run'):
            raise ValueError('Unknown run status')
        runs[status] += 1
        assessment = result.get('knowledgeAssessment') or {}
        checks = assessment.get('knowledgeChecks', [])
        used = set()
        for expected in case['expected'].get('knowledge', []):
            assertion_count += 1
            candidates = [(i, c) for i, c in enumerate(checks) if i not in used and c.get('round') == expected['round']
                          and c.get('topic', c.get('field')) == expected['field']
                          and c.get('originalQuote', '').strip()
                          and (c['originalQuote'].strip() in expected['quote'].strip()
                               or expected['quote'].strip() in c['originalQuote'].strip())]
            match = candidates[0] if len(candidates) == 1 else None
            verdict = match[1].get('verdict') if match else None
            if match:
                used.add(match[0])
            # A reported competitor quote/budget is not our factual assertion. The
            # production extractor may correctly exclude it, without emitting a check.
            # Missing a positive/negative factual assertion always remains a failure.
            if expected['verdict'] == 'not_applicable' and not candidates and status in ('ok', 'offline_only'):
                other_checks = [c for c in checks if c.get('round') == expected['round']
                                and c.get('topic', c.get('field')) == expected['field']]
                if not other_checks:
                    verdict = 'not_applicable'
            correct = verdict == expected['verdict']
            matched_count += correct
            if expected['verdict'] == 'contradicted':
                wrong_count += 1
                missed_count += verdict != 'contradicted'
            if expected['verdict'] in ('supported', 'not_applicable'):
                safe_count += 1
                penalty_count += verdict in ('contradicted', 'incomplete')
            assessments.append({'caseId': run_key[0], 'repetition': run_key[1], 'round': expected['round'],
                                'field': expected['field'], 'expected': expected['verdict'], 'actual': verdict,
                                'passed': correct, 'quote': expected['quote'],
                                'actualQuote': match[1].get('originalQuote') if match else None})
        for i, check in enumerate(checks):
            if i not in used:
                unexpected_checks.append({'caseId': run_key[0], 'repetition': run_key[1], 'check': check})
        if case['expected'].get('unscored'):
            if results['runMode'] == 'offline':
                unscored_pending += 1
            else:
                unscored_total += 1
                report = result.get('report') or {}
                unscored_correct += ('totalScore' in report and report['totalScore'] is None and
                                     'passed' in report and report['passed'] is None and
                                     report.get('scoreBasis') == 'communication_only')
        if status == 'ok' and case['mode'] == 'training':
            dimensions = (result.get('report') or {}).get('dimensionScores', {})
            for dimension, value in dimensions.items():
                if isinstance(value, (int, float)) and not isinstance(value, bool):
                    scores.setdefault(case['id'], {}).setdefault(dimension, []).append(value)
    stability = {case_id: {dim: {'n': len(values), 'median': statistics.median(values),
                               'min': min(values), 'max': max(values)} for dim, values in dims.items()}
                 for case_id, dims in scores.items()}
    pairs = []
    pair_correct = pair_total = ties = 0
    knowledge_pair_correct = knowledge_pair_total = knowledge_ties = 0
    for pair in fixture.get('pairs', []):
        hi, lo, dim = pair['higherCaseId'], pair['lowerCaseId'], pair['dimension']
        if hi not in cases or lo not in cases:
            continue
        a, b = scores.get(hi, {}).get(dim, []), scores.get(lo, {}).get(dim, [])
        expected_a, expected_b = cases[hi].get('repetitions', 1), cases[lo].get('repetitions', 1)
        if len(a) != expected_a or len(b) != expected_b:
            pairs.append(dict(pair, status='not_evaluated', higherN=len(a), lowerN=len(b)))
            continue
        high, low = statistics.median(a), statistics.median(b)
        if dim == 'knowledgeAccuracy':
            knowledge_pair_total += 1
            knowledge_pair_correct += high > low
            knowledge_ties += high == low
        else:
            pair_total += 1
            pair_correct += high > low
            ties += high == low
        pairs.append(dict(pair, status='passed' if high > low else 'tie' if high == low else 'reversed',
                          higherMedian=high, lowerMedian=low))
    eligible = {k for k, r in actual.items() if r.get('status') == 'ok'}
    completed = set()
    claims_supported = claims_total = events_correct = events_total = forbidden_count = 0
    error_counts = dict.fromkeys(['misattribution', 'correctionState', 'fabricatedResolution', 'falseViolation', 'missedViolation'], 0)
    for row in (review or {}).get('reviews', []):
        k = key(row)
        if k not in eligible:
            raise ValueError('Review references an unavailable report')
        if row.get('status') != 'complete':
            continue
        if k in completed or not row.get('reviewer', '').strip():
            raise ValueError('Completed review needs unique run and reviewer')
        case = cases[k[0]]
        expected = case['expected'].get('summary', {})
        for name, source, answer in [('events', 'mustMention', 'covered'), ('forbiddenClaims', 'mustNotClaim', 'occurred')]:
            entries = row.get(name, [])
            if sorted(x['id'] for x in entries) != sorted(x['id'] for x in expected.get(source, [])):
                raise ValueError('Review omitted or changed frozen event IDs')
            if any(type(x.get(answer)) is not bool for x in entries):
                raise ValueError('Complete review contains unreviewed events')
        claims = row.get('claims', [])
        if not claims or any(not x.get('text') or type(x.get('supported')) is not bool
                             or not isinstance(x.get('sourceRounds'), list) for x in claims):
            raise ValueError('Complete review requires all atomic claims, support labels and source rounds')
        rounds = {m['round'] for m in case['history']}
        if any(any(type(n) is not int or n not in rounds for n in x['sourceRounds'])
               or (x['supported'] and not x['sourceRounds']) for x in claims):
            raise ValueError('Supported claims need valid source rounds from the frozen dialogue')
        if any(type(row.get('errors', {}).get(field)) is not bool for field in error_counts):
            raise ValueError('Complete review contains unreviewed error flags')
        completed.add(k)
        claims_total += len(claims)
        claims_supported += sum(x['supported'] for x in claims)
        events_total += len(row['events'])
        events_correct += sum(x['covered'] for x in row['events'])
        forbidden_count += sum(x['occurred'] for x in row['forbiddenClaims'])
        for field in error_counts:
            error_counts[field] += row['errors'][field]
    hard_failures = assertion_count - matched_count + unscored_total - unscored_correct + runs['error'] + runs['not_run']
    hard_failures += claims_total - claims_supported + events_total - events_correct + forbidden_count + sum(error_counts.values())
    return {'schemaVersion': 1, 'suiteId': fixture['suiteId'], 'runMode': results['runMode'], 'runs': runs,
            'knowledgeAccuracy': rate(matched_count, assertion_count), 'errorMissRate': rate(missed_count, wrong_count),
            'falsePenaltyRate': rate(penalty_count, safe_count), 'unscoredHandling': rate(unscored_correct, unscored_total),
            'unscoredPending': unscored_pending,
            'pairConsistency': rate(pair_correct, pair_total), 'pairTies': ties,
            'knowledgePairConsistency': rate(knowledge_pair_correct, knowledge_pair_total),
            'knowledgePairTies': knowledge_ties,
            'communicationStatus': 'needs_review' if pair_correct < pair_total else
                'pending' if any(p['status'] == 'not_evaluated' and p['dimension'] != 'knowledgeAccuracy' for p in pairs)
                or not pair_total else 'passed',
            'summaryFactAccuracy': rate(claims_supported, claims_total), 'summaryEventCoverage': rate(events_correct, events_total),
            'review': {'complete': len(completed), 'pending': len(eligible - completed),
                       'status': 'complete' if eligible and completed == eligible else 'pending',
                       'forbiddenClaims': forbidden_count, 'errorCounts': error_counts},
            'hardFailures': hard_failures, 'assertions': assessments, 'pairs': pairs, 'scoreStability': stability,
            'unmatchedChecksNeedReview': unexpected_checks, 'limits': results.get('limits', [])}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run_dir', type=Path)
    parser.add_argument('--review', type=Path)
    parser.add_argument('--output', type=Path, help='New report path; existing paths are rejected')
    parser.add_argument('--require-review', action='store_true')
    args = parser.parse_args()
    input_path, results_path = args.run_dir / 'input.json', args.run_dir / 'results.json'
    fixture, results = json.loads(input_path.read_text()), json.loads(results_path.read_text())
    manifest = json.loads((args.run_dir / 'manifest.json').read_text())
    if manifest['inputHash'] != sha(input_path):
        raise ValueError('Frozen input hash mismatch')
    input_hash, results_hash = sha(input_path), sha(results_path)
    review = json.loads(args.review.read_text()) if args.review else None
    if review and (review.get('resultsHash') != results_hash or review.get('inputHash') != input_hash):
        raise ValueError('Review is bound to a different input or result')
    metrics = evaluate(fixture, results, review)
    metrics.update(inputHash=input_hash, resultsHash=results_hash)
    output = args.output or args.run_dir / 'metrics.json'
    with output.open('x', encoding='utf8') as out:
        json.dump(metrics, out, ensure_ascii=False, indent=2)
        out.write('\n')
    template_path = args.run_dir / 'review-template.json'
    if not template_path.exists():
        with template_path.open('x', encoding='utf8') as out:
            json.dump(review_template(fixture, results, results_hash, input_hash), out, ensure_ascii=False, indent=2)
            out.write('\n')
    print(json.dumps({k: metrics[k] for k in ['runs', 'knowledgeAccuracy', 'errorMissRate', 'falsePenaltyRate',
                                            'pairConsistency', 'summaryFactAccuracy', 'summaryEventCoverage', 'review', 'hardFailures']}, ensure_ascii=False))
    return 1 if metrics['hardFailures'] else 2 if args.require_review and metrics['review']['status'] != 'complete' else 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (ValueError, KeyError, OSError) as error:
        print('Evaluation failed: ' + str(error), file=sys.stderr)
        sys.exit(2)
