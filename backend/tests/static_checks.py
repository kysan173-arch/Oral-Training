#!/usr/bin/env python3
"""Portable client syntax/JSON checks and existing client behavior regressions."""
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
count = {'javascript': 0, 'json': 0}
for path in ROOT.rglob('*'):
    relative = path.relative_to(ROOT)
    if any(p in {'.git', 'node_modules', 'tmp', '.local-postgres-data', '.runtime'} or p.startswith('build')
           for p in relative.parts):
        continue
    if path.suffix == '.js':
        subprocess.run(['node', '--check', str(path)], check=True, capture_output=True)
        count['javascript'] += 1
    elif path.suffix == '.json':
        json.loads(path.read_text(encoding='utf-8-sig'))
        count['json'] += 1
for name in ['client_recovery', 'patient_client', 'knowledge_report_client', 'audit_client_regression']:
    subprocess.run(['node', str(ROOT / 'backend/tests' / (name + '_test.js'))], check=True)
print(json.dumps({'result': 'passed', **count}))
