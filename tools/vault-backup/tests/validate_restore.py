#!/usr/bin/env python3
"""Isolated restoration validation; prints only aggregate, non-secret results."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import sqlite3
import subprocess
import tarfile
import tempfile

os.umask(0o077)
source = Path('/home/vaultadmin/vault-backup-restore-test/restore-test.tar.zst')
with tempfile.TemporaryDirectory(prefix='restore-check-', dir='/var/backups/bitwarden') as directory:
    folder = Path(directory)
    archive_path = folder / 'restore.tar'
    subprocess.run(['zstd', '-q', '-t', str(source)], check=True)
    subprocess.run(['zstd', '-q', '-d', str(source), '-o', str(archive_path)], check=True)
    extracted = folder / 'extracted'
    extracted.mkdir(mode=0o700)
    with tarfile.open(archive_path) as archive:
        members = archive.getmembers()
        for member in members:
            path = Path(member.name)
            if path.is_absolute() or '..' in path.parts or not (member.isdir() or member.isfile()):
                raise RuntimeError('Unsafe or unsupported archive entry')
        archive.extractall(extracted, filter='data')
    base = extracted / 'bitwarden'
    manifest = json.loads((extracted / 'manifest.json').read_text())
    for name in ('data/vault.db', 'settings.env', 'compose.yaml'):
        if not (base / name).is_file():
            raise RuntimeError('Required deployment file absent')
    assert manifest['data_uid'] == 911 and manifest['data_gid'] == 911
    assert manifest['sqlite_integrity'] == 'ok'
    db = sqlite3.connect(base / 'data/vault.db')
    try:
        assert db.execute('PRAGMA integrity_check').fetchone()[0] == 'ok'
        accounts = db.execute('SELECT COUNT(*) FROM "User"').fetchone()[0]
        assert accounts == 1
    finally:
        db.close()
    # Compare captured non-database, non-log files with the live deployment.
    # This covers configuration, server keys and attachments without printing them.
    compared = keys = attachments = 0
    def digest(path):
        h = hashlib.sha256()
        with path.open('rb') as f:
            for block in iter(lambda: f.read(1024*1024), b''):
                h.update(block)
        return h.digest()
    for path in base.rglob('*'):
        if not path.is_file():
            continue
        relative = path.relative_to(base)
        name = str(relative).lower()
        if 'logs/' in name or path.name.startswith('vault.db'):
            continue
        live = Path('/opt/bitwarden') / relative
        assert live.is_file() and digest(path) == digest(live), 'Captured file comparison failed'
        compared += 1
        if any(word in name for word in ('key', '.pem', '.pfx', 'dataprotection')):
            keys += 1
        if 'attachments/' in name:
            attachments += 1
    assert keys > 0, 'No captured server key files detected'
    print(json.dumps({'freeze_seconds': manifest.get('freeze_seconds'), 'zstd_validation': 'ok', 'sqlite_integrity': 'ok', 'accounts': accounts,
                      'required_files': 'ok', 'unchanged_files_compared': compared,
                      'server_key_files_verified': keys, 'attachment_files_verified': attachments,
                      'data_uid_gid': '911:911', 'isolated_plaintext_cleanup': 'on context exit'}))
