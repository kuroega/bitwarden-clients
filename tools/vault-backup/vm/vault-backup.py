#!/usr/bin/env python3
"""Root-owned, fixed-scope consistent export; stdout is a zstd archive only."""
import datetime
import fcntl
import json
import os
import shutil
import signal
import sqlite3
import subprocess
import sys
import tarfile
import tempfile
import time
from pathlib import Path


def run(args, **kwargs):
    # Only fixed command names/actions reach diagnostics. Raw stderr and argv
    # can contain deployment secrets and must never reach GUI/history output.
    action = {
        ('docker', 'inspect'): 'Docker container inspection',
        ('docker', 'pause'): 'Container pause',
        ('docker', 'unpause'): 'Container resume',
        ('tar', '-cf'): 'Snapshot creation',
        ('tar', '-rf'): 'Manifest append',
    }.get(tuple(args[:2]), 'Archive compression' if '-o' in args else 'Archive validation')
    timeout = kwargs.pop('timeout', 300)
    try:
        return subprocess.run(args, check=True, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, timeout=timeout, **kwargs)
    except subprocess.CalledProcessError as error:
        stderr = error.stderr or b''
        if isinstance(stderr, bytes):
            stderr = stderr.decode('utf-8', errors='replace')
        if args[0] == 'docker' and any(marker in stderr.lower() for marker in (
                'cannot connect to the docker daemon', 'failed to connect to the docker api',
                'error during connect')):
            raise RuntimeError('Docker daemon is unavailable. On the VM, manually run '
                               'sudo systemctl start docker, then sudo docker start bitwarden; '
                               'retry the backup after Bitwarden is ready.') from None
        raise RuntimeError(f'{action} failed (exit code {error.returncode}); '
                           'check the VM service state, disk space and permissions.') from None
    except subprocess.TimeoutExpired:
        raise RuntimeError(f'{action} timed out after {timeout} seconds.') from None


def interrupted(signum, frame):
    raise RuntimeError('Backup interrupted')


def main():
    if os.geteuid() != 0 or len(sys.argv) != 1:
        raise RuntimeError('Run the fixed helper as root, without arguments')
    os.umask(0o077)
    os.environ['PATH'] = '/usr/sbin:/usr/bin:/sbin:/bin'
    for name in ('tar', 'zstd', 'docker'):
        if not shutil.which(name):
            raise RuntimeError('Missing required program: ' + name)
    for sig in (signal.SIGTERM, signal.SIGHUP, signal.SIGINT):
        signal.signal(sig, interrupted)
    with open('/run/vault-backup.lock', 'a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        export()


def export():
    info = json.loads(run(['docker', 'inspect', 'bitwarden'], timeout=30).stdout)[0]
    if not info['State']['Running'] or info['State']['Paused']:
        raise RuntimeError('Bitwarden must already be running and unpaused. Check '
                           'sudo docker inspect --format "{{.State.Running}} {{.State.Paused}}" '
                           'bitwarden; manually start a stopped container with '
                           'sudo docker start bitwarden or resume a paused container with '
                           'sudo docker unpause bitwarden before retrying.')
    base = Path('/opt/bitwarden')
    mounts = info['Mounts']
    if not any(m['Destination'] == '/etc/bitwarden' and m['Source'] == str(base / 'data') for m in mounts):
        raise RuntimeError('Unexpected persistent-data mount; refusing backup')
    for item in ('data', 'settings.env', 'compose.yaml'):
        if (base / item).is_symlink() or not (base / item).exists():
            raise RuntimeError('Missing or unexpected deployment path: ' + item)
    parent = Path('/var/backups/bitwarden')
    if parent.is_symlink():
        raise RuntimeError('Unexpected backup staging symlink')
    parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    os.chmod(parent, 0o700)
    with tempfile.TemporaryDirectory(prefix='manual-', dir=parent) as directory:
        folder = Path(directory)
        raw, compressed = folder / 'vault.tar', folder / 'vault.tar.zst'
        paused = False
        freeze_started = time.monotonic()
        try:
            # No writer changes SQLite/WAL, keys, or attachments during the copy.
            # Mark before invoking Docker so ambiguous command failures also resume.
            paused = True
            run(['docker', 'pause', 'bitwarden'], timeout=30)
            run(['tar', '-cf', str(raw), '-C', '/opt', 'bitwarden/data',
                 'bitwarden/settings.env', 'bitwarden/compose.yaml'], timeout=300)
        finally:
            if paused:
                # Suppress termination only during the mandatory resume operation.
                for sig in (signal.SIGTERM, signal.SIGHUP, signal.SIGINT):
                    signal.signal(sig, signal.SIG_IGN)
                try:
                    resumed = False
                    for attempt in range(3):
                        try:
                            state = json.loads(run(['docker', 'inspect', '--format', '{{json .State}}', 'bitwarden'], timeout=30).stdout)
                            if state['Paused']:
                                run(['docker', 'unpause', 'bitwarden'], timeout=30)
                            state = json.loads(run(['docker', 'inspect', '--format', '{{json .State}}', 'bitwarden'], timeout=30).stdout)
                            if state['Running'] and not state['Paused']:
                                resumed = True
                                break
                        except Exception:
                            time.sleep(1)
                    if not resumed:
                        raise RuntimeError('RESUME FAILED: run sudo docker unpause bitwarden manually')
                finally:
                    for sig in (signal.SIGTERM, signal.SIGHUP, signal.SIGINT):
                        signal.signal(sig, interrupted)
        freeze_seconds = round(time.monotonic() - freeze_started, 3)
        # Check the captured DB (including WAL), rather than the live database.
        check = folder / 'check'
        check.mkdir()
        with tarfile.open(raw, 'r:') as archive:
            names = set(archive.getnames())
            if 'bitwarden/data/vault.db' not in names:
                raise RuntimeError('Database is absent from captured archive')
            for suffix in ('', '-wal', '-shm'):
                name = 'bitwarden/data/vault.db' + suffix
                if name in names:
                    member = archive.getmember(name)
                    if not member.isfile():
                        raise RuntimeError('Unexpected SQLite file type')
                    with archive.extractfile(member) as source, open(check / ('vault.db' + suffix), 'wb') as dest:
                        shutil.copyfileobj(source, dest)
        connection = sqlite3.connect(check / 'vault.db')
        try:
            if connection.execute('PRAGMA integrity_check').fetchone()[0] != 'ok':
                raise RuntimeError('Captured SQLite integrity check failed')
        finally:
            connection.close()
        manifest = {
            'format': 1, 'created_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
            'image': info['Config']['Image'], 'container': 'bitwarden',
            'database': 'bitwarden/data/vault.db', 'sqlite_integrity': 'ok',
            'compression': 'zstd -19 --single-thread',
            'data_uid': (base / 'data').stat().st_uid, 'data_gid': (base / 'data').stat().st_gid,
            'consistency': 'Container paused during complete tar snapshot, resumed before compression',
            'freeze_seconds': freeze_seconds,
        }
        (folder / 'manifest.json').write_text(json.dumps(manifest, indent=2))
        run(['tar', '-rf', str(raw), '-C', str(folder), 'manifest.json'])
        run(['zstd', '-q', '-19', '--single-thread', str(raw), '-o', str(compressed)], timeout=1800)
        run(['zstd', '-q', '-t', str(compressed)], timeout=300)
        with open(compressed, 'rb') as source:
            shutil.copyfileobj(source, sys.stdout.buffer, length=1024 * 1024)
        sys.stdout.buffer.flush()
        print('Consistent SQLite/attachments/configuration snapshot; container resumed; zstd validated.', file=sys.stderr)


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        # Never print deployment contents or Docker environment.
        message = str(error) if isinstance(error, RuntimeError) else type(error).__name__
        print('Backup failed: ' + message, file=sys.stderr)
        sys.exit(1)
