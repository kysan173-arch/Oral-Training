#!/usr/bin/env python3
"""Linux backend launcher. Config is data, never evaluated as shell code."""
import argparse
import os
from pathlib import Path
import re
import secrets
import stat

ROOT = Path(__file__).resolve().parent


def load_env(path):
    env = os.environ.copy()
    for number, raw in enumerate(path.read_text(encoding='utf-8-sig').splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith('#'):
            continue
        name, separator, value = line.partition('=')
        name, value = name.strip(), value.strip()
        if not separator or not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', name):
            raise ValueError(f'Invalid configuration at line {number}')
        if len(value) >= 2 and value[0] in ('"', "'") and value[-1] == value[0]:
            value = value[1:-1]
        env[name] = value
    return env


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--env-file', type=Path, default=ROOT / 'backend.env')
    parser.add_argument('--binary', type=Path, default=ROOT / 'build/oral_training_backend')
    parser.add_argument('--init-key', type=Path, help='Create a new 32-byte key, refusing to overwrite any file')
    args = parser.parse_args()
    if args.init_key:
        path = args.init_key.expanduser().absolute()
        path.parent.mkdir(parents=True, mode=0o700, exist_ok=True)
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
        with os.fdopen(fd, 'wb') as output:
            output.write(secrets.token_bytes(32))
            output.flush()
            os.fsync(output.fileno())
        print(f'Created private master key: {path}. Back it up separately from the database.')
        return
    env = load_env(args.env_file)
    if not env.get('DATABASE_URL') or '<password>' in env['DATABASE_URL']:
        raise ValueError('Set DATABASE_URL in the configuration file')
    key = Path(env.get('MODEL_KEY_FILE', ''))
    if not key.is_absolute():
        raise ValueError('Set MODEL_KEY_FILE to an absolute path')
    info = key.lstat()
    if not stat.S_ISREG(info.st_mode) or info.st_uid != os.geteuid() or info.st_mode & 0o077 or info.st_size != 32:
        raise ValueError('MODEL_KEY_FILE must be a private, owner-held 32-byte file (mode 0600)')
    binary = args.binary.resolve()
    if not binary.is_file():
        raise ValueError('Build the backend first: cmake --build backend/build -j 2')
    os.execve(binary, [str(binary)], env)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError) as error:
        raise SystemExit(f'Backend startup failed: {error}')
