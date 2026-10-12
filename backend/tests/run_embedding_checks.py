#!/usr/bin/env python3
"""Run offline checks and optional pass-backed model tests in a disposable DB; build first."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', type=Path, default=ROOT / 'backend/build')
    parser.add_argument('--pg-bin', type=Path, default=Path('/usr/lib/postgresql/16/bin'))
    parser.add_argument('--live', action='store_true', help='Real model smoke or conversation suite after offline gates pass')
    parser.add_argument('--pass-entry')
    parser.add_argument('--provider', choices=['minimax','deepseek'], default='minimax')
    parser.add_argument('--model')
    parser.add_argument('--suite', type=Path, default=ROOT/'backend/tests/fixtures/rag_conversations.json')
    parser.add_argument('--conversations', choices=['development','holdout','all'])
    parser.add_argument('--run-name', default='embedding-live')
    args = parser.parse_args()
    if not args.run_name.replace('-', '').replace('_', '').isalnum():
        parser.error('run-name must use letters, numbers, hyphens or underscores')
    if args.conversations and not args.live:
        parser.error('--conversations requires --live')
    pass_entry = args.pass_entry or ('api/deepseek' if args.provider == 'deepseek' else 'api/minimax')
    model_name = args.model or ('deepseek-flash' if args.provider == 'deepseek' else 'MiniMax-M2.7')
    base_url = 'https://api.deepseek.com' if args.provider == 'deepseek' else 'https://api.minimax.cn/v1'
    if args.provider == 'deepseek' and args.live and not args.conversations:
        parser.error('DeepSeek live verification requires --conversations')
    suite_bytes = args.suite.resolve().read_bytes()
    build = args.build.resolve()
    log_path = build / 'embedding-checks.log'
    with tempfile.TemporaryDirectory(prefix='oral-embedding-check-') as temp, log_path.open('w') as log:
        temp = Path(temp)
        socket = temp / 'socket'
        socket.mkdir()
        data = temp / 'data'
        def run(command, **kwargs):
            return subprocess.run([str(x) for x in command], check=True, stdout=log,
                                  stderr=subprocess.STDOUT, **kwargs)
        started = False
        try:
            run([args.pg_bin / 'initdb', '-D', data, '-A', 'trust', '--no-locale', '-E', 'UTF8'])
            run([args.pg_bin / 'pg_ctl', '-D', data, '-l', temp / 'postgres.log',
                 '-o', f"-F -c listen_addresses='' -k {socket}", '-w', 'start'])
            started = True
            run([args.pg_bin / 'createdb', '-h', socket, 'oral_embedding_test'])
            db = f'host={socket} dbname=oral_embedding_test'
            psql = [args.pg_bin / 'psql', '-X', '--dbname', db, '-v', 'ON_ERROR_STOP=1']
            for migration in sorted((ROOT / 'backend/migrations').glob('[0-9][0-9][0-9]_*.sql')):
                run(psql + ['-f', migration])
            env = dict(os.environ, ORAL_TRAINING_TEST_DATABASE_URL=db, ORAL_EMBEDDING_TEST_ISOLATED='1')
            # Test configurations must be deterministic, not inherit a developer's tuning.
            for name in list(env):
                if name.startswith('RAG_EMBEDDING_'):
                    env.pop(name)
            result = subprocess.run(['ctest', '--test-dir', str(build), '--output-on-failure', '-R',
                                     '^(embedding_client|embedding_rag|rag_contract|evidence_validator|patient_grounding|knowledge_evaluator|rag_retriever|rag_conversation_evaluator)$'],
                                    env=env, text=True, capture_output=True)
            print(result.stdout, end='')
            log.write(result.stdout + result.stderr)
            result.check_returncode()
            # Idempotence with populated cache, without changing source data.
            def counts():
                return subprocess.check_output(psql + ['-Atc',
                    'SELECT (SELECT count(*) FROM knowledge_revisions), (SELECT count(*) FROM knowledge_chunk_embeddings)'], text=True)
            before = counts()
            run(psql + ['-f', ROOT / 'backend/migrations/036_embeddinggemma_cache.sql'])
            assert counts() == before, 'Migration rerun changed source or cache rows'
            print('All numbered migrations + populated cache migration rerun passed.')
            if args.live:
                secret = subprocess.run(['pass', 'show', pass_entry], capture_output=True, text=True, timeout=20)
                if secret.returncode:
                    raise RuntimeError('Unable to decrypt the selected pass entry')
                key = secret.stdout.splitlines()[0].strip()
                if not key.startswith('sk-'):
                    raise RuntimeError('Expected the API key on the first line of the pass entry')
                live_env = dict(env, LIVE_MODEL_API_KEY=key, LIVE_MODEL_NAME=model_name, LIVE_MODEL_BASE_URL=base_url)
                if not args.conversations:
                    live_env['MINIMAX_API_KEY'] = key
                    live_env['MINIMAX_MODEL'] = model_name
                # Never pass the key as an argv value or save it to an env file.
                output_path = build / (args.run_name + '.jsonl')
                if output_path.exists():
                    raise RuntimeError('Choose a new --run-name to preserve the previous evidence')
                knowledge = ROOT / 'docs/rag-data/患者模拟_RAG测试知识库_具体参数版.json'
                suite = output_path.with_suffix('.suite.json')
                suite.write_bytes(suite_bytes)
                binary = build / ('rag_conversation_probe' if args.conversations else 'embedding_rag_probe')
                command = [str(binary), '--live', str(knowledge)]
                if args.conversations:
                    command += [str(suite), args.conversations]
                sources = ['backend/src/rag_retriever.cpp', 'backend/src/service_reply_prompt.h',
                           'backend/src/model_completion.h', 'backend/src/evidence_validator.h',
                           'backend/src/patient_prompt.h', 'backend/src/patient_grounding.h',
                           'backend/tests/live_model_fixture.h',
                           'backend/tests/evaluate_rag_conversations.py', 'backend/src/rag_types.h',
                           'backend/tests/rag_conversation_probe.cpp']
                hashes = {name: hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in sources}
                hashes[str(suite)] = hashlib.sha256(suite_bytes).hexdigest()
                hashes['executable'] = hashlib.sha256(binary.read_bytes()).hexdigest()
                output_path.with_suffix('.manifest.json').write_text(json.dumps(hashes,indent=2))
                with output_path.open('w') as output:
                    probe = subprocess.run(command,
                        env=live_env, stdout=output, stderr=subprocess.PIPE, text=True, timeout=3600 if args.conversations else 240)
                live_env.pop('MINIMAX_API_KEY', None)
                live_env.pop('LIVE_MODEL_API_KEY', None)
                if probe.returncode:
                    # Probe errors are intentionally limited to status/schema diagnostics.
                    print(probe.stderr, end='')
                    probe.check_returncode()
                print(f'Live EmbeddingGemma + {model_name} results: {output_path}')
                if args.conversations:
                    evaluation = subprocess.run(['python3', '-B', str(ROOT/'backend/tests/evaluate_rag_conversations.py'),
                        str(output_path), '--cohort', args.conversations, '--suite', str(suite)])
                    if evaluation.returncode:
                        raise SystemExit(evaluation.returncode)
        finally:
            if started:
                run([args.pg_bin / 'pg_ctl', '-D', data, '-m', 'fast', '-w', 'stop'])
            print(f'Diagnostic log: {log_path}')


if __name__ == '__main__':
    main()
