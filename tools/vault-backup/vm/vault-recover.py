#!/usr/bin/env python3
"""Fixed-scope recovery. Receive a zstd snapshot on stdin; never log secrets."""
import fcntl
import configparser
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import signal
import sqlite3
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.request
from urllib.parse import urlsplit

BASE = Path('/opt/bitwarden')
ROOT = Path('/var/backups/bitwarden')
LOCK = Path('/run/vault-backup.lock')
CONFIG = Path('/etc/vault-backup/server.local.ini')
URL = ''


def load_server_url(path=CONFIG):
    """Read trusted machine configuration before changing the deployment."""
    try:
        info = path.lstat()
        if not path.is_file() or path.is_symlink() or info.st_uid != 0 or info.st_mode & 0o022:
            raise ValueError('Untrusted configuration')
        config = configparser.ConfigParser(interpolation=None)
        with path.open(encoding='utf-8-sig') as source:
            config.read_file(source)
        url = config['server']['vault_url']
        parsed = urlsplit(url)
        if (parsed.scheme != 'https' or not parsed.hostname or parsed.username or
                parsed.password or parsed.path or parsed.query or parsed.fragment or
                any(c.isspace() for c in url)):
            raise ValueError('Invalid HTTPS origin')
        return url
    except (OSError, KeyError, ValueError, configparser.Error):
        raise RuntimeError('Missing or invalid /etc/vault-backup/server.local.ini') from None


def report(percent):
    # A lost client must not abort recovery or rollback.
    try:
        print('RECOVER_PROGRESS ' + str(percent), flush=True)
    except BrokenPipeError:
        pass


def report_warning():
    try:
        print('RECOVER_WARNING old-container-retained', flush=True)
    except BrokenPipeError:
        pass


def run(args, timeout=120):
    try:
        return subprocess.run(args, check=True, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, timeout=timeout).stdout
    except (subprocess.SubprocessError, OSError):
        raise RuntimeError('Command failed: ' + args[0]) from None


def unpack(archive, target):
    """Extract only regular deployment files, never links/devices/path traversal."""
    with tarfile.open(archive) as tar:
        seen = set()
        for member in tar:
            name = member.name.rstrip('/')
            parts = PurePosixPath(name).parts
            if (not parts or name.startswith('/') or '..' in parts or
                    name in seen or '\\' in name or
                    not (name in ('manifest.json', 'bitwarden', 'bitwarden/data',
                                  'bitwarden/settings.env', 'bitwarden/compose.yaml') or
                         name.startswith('bitwarden/data/')) or
                    not (member.isdir() or member.isfile())):
                raise RuntimeError('Unsafe archive entry')
            seen.add(name)
            path = target / name
            if member.isdir():
                path.mkdir(mode=0o700, parents=True, exist_ok=True)
            else:
                path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
                with tar.extractfile(member) as source, open(path, 'xb') as dest:
                    shutil.copyfileobj(source, dest)
                os.chmod(path, member.mode & 0o777 & ~0o002 or 0o600)
        required = {'manifest.json', 'bitwarden/data/vault.db',
                    'bitwarden/settings.env', 'bitwarden/compose.yaml'}
        if not required <= seen:
            raise RuntimeError('Required backup files are missing')
    manifest = json.loads((target / 'manifest.json').read_text())
    image = manifest.get('image', '')
    if (manifest.get('format') != 1 or manifest.get('container') != 'bitwarden' or
            not re.fullmatch(r'ghcr\.io/bitwarden/lite@sha256:[0-9a-f]{64}', image)):
        raise RuntimeError('Unsupported backup manifest/image')
    db = target / 'bitwarden/data/vault.db'
    connection = sqlite3.connect(db)
    try:
        if connection.execute('PRAGMA integrity_check').fetchone()[0] != 'ok':
            raise RuntimeError('SQLite integrity check failed')
    finally:
        connection.close()
    return image


def healthy():
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    for _ in range(90):
        try:
            state = json.loads(run(['docker', 'inspect', '--format', '{{json .State}}', 'bitwarden'], 30))
            if state['Running'] and not state['Paused']:
                with opener.open(URL + '/api/config', timeout=5) as response:
                    config = json.load(response)
                if config.get('version') and config.get('environment', {}).get('vault') == URL:
                    return
        except (RuntimeError, OSError, ValueError):
            pass
        time.sleep(2)
    raise RuntimeError('Private HTTPS/API health check failed')


def restore(deployment, image, folder):
    # A fixed docker invocation avoids executing archived Compose directives.
    try:
        run(['docker', 'image', 'inspect', image], 30)
    except RuntimeError:
        run(['docker', 'pull', image], 600)
    report(70)
    old = json.loads(run(['docker', 'ps', '-a', '--filter', 'name=^/bitwarden$', '--format', 'json'], 30) or b'null')
    running = paused = False
    if old:
        info = json.loads(run(['docker', 'inspect', 'bitwarden'], 30))[0]
        if not any(m['Source'] == str(BASE / 'data') and m['Destination'] == '/etc/bitwarden' for m in info['Mounts']):
            raise RuntimeError('Unexpected existing container mount')
        running = info['State']['Running']
        paused = info['State']['Paused']
    for path in (BASE, ROOT):
        if path.is_symlink():
            raise RuntimeError('Unexpected deployment symlink')
    suffix = folder.name
    previous = BASE.with_name('bitwarden-before-' + suffix)
    old_name = 'bitwarden-before-' + suffix
    journal = folder / 'status.json'
    def status(phase):
        temp = journal.with_suffix('.partial')
        temp.write_text(json.dumps({'phase': phase, 'previous': str(previous),
                                    'old_container': old_name, 'was_running': running, 'was_paused': paused}))
        os.replace(temp, journal)
        with open(journal, 'rb') as file:
            os.fsync(file.fileno())
    status('prepared')
    moved_container = moved_data = new_data = new_container = False
    try:
        report(75)
        if old:
            if paused:
                run(['docker', 'unpause', 'bitwarden'], 30)
            if running:
                run(['docker', 'stop', '-t', '30', 'bitwarden'], 60)
            run(['docker', 'rename', 'bitwarden', old_name], 30)
            moved_container = True
        status('old-container-stopped')
        if BASE.exists():
            BASE.rename(previous)
            moved_data = True
        status('old-data-retained')
        deployment.rename(BASE)
        new_data = True
        os.chmod(BASE, 0o700)
        for name in ('settings.env', 'compose.yaml'):
            os.chmod(BASE / name, 0o600)
        for directory, dirs, files in os.walk(BASE / 'data'):
            os.chown(directory, 911, 911)
            for name in files:
                os.chown(Path(directory) / name, 911, 911)
        status('data-installed')
        report(85)
        new_container = True  # Creation can succeed even when its response times out.
        run(['docker', 'create', '--name', 'bitwarden', '--restart', 'no',
             '--label', 'com.docker.compose.project=bitwarden',
             '--label', 'com.docker.compose.service=bitwarden',
             '--memory', '1536m', '--log-opt', 'max-size=10m', '--log-opt', 'max-file=3',
             '-p', '127.0.0.1:8080:8080', '--env-file', str(BASE / 'settings.env'),
             '-v', str(BASE / 'data') + ':/etc/bitwarden', image], 60)
        run(['docker', 'start', 'bitwarden'], 60)
        run(['tailscale', 'serve', '--bg', '--https=443', 'http://127.0.0.1:8080'], 60)
        status('checking-service')
        report(90)
        healthy()
        status('complete')
        report(98)
    except BaseException:
        try:
            if new_container:
                try:
                    run(['docker', 'rm', '-f', 'bitwarden'], 60)
                except RuntimeError:
                    if run(['docker', 'ps', '-a', '--filter', 'name=^/bitwarden$', '--format', '{{.ID}}'], 30).strip():
                        raise
            if new_data:
                BASE.rename(folder / 'failed-deployment')
            if moved_data:
                previous.rename(BASE)
            if moved_container:
                run(['docker', 'rename', old_name, 'bitwarden'], 30)
            if old and running:
                run(['docker', 'start', 'bitwarden'], 60)
                if paused:
                    run(['docker', 'pause', 'bitwarden'], 30)
            status('rolled-back')
        except BaseException:
            status('rollback-failed')
            raise RuntimeError('ROLLBACK FAILED; inspect ' + str(journal)) from None
        raise RuntimeError('Recovery failed; previous deployment restored. Inspect ' + str(journal)) from None
    # Cleanup is outside the transaction: never roll back after retiring the
    # previous container. The old deployment files remain available.
    if moved_container:
        try:
            run(['docker', 'rm', old_name], 30)
        except RuntimeError:
            try:
                status('complete-old-container-retained')
            except OSError:
                pass
            report_warning()
    return previous


def main():
    global URL
    if os.geteuid() != 0 or len(sys.argv) != 1:
        raise RuntimeError('Run as root without arguments')
    os.umask(0o077)
    os.environ['PATH'] = '/usr/sbin:/usr/bin:/sbin:/bin'
    URL = load_server_url()
    if ROOT.is_symlink():
        raise RuntimeError('Unexpected staging symlink')
    ROOT.mkdir(mode=0o700, parents=True, exist_ok=True)
    os.chmod(ROOT, 0o700)
    with open(LOCK, 'a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        for journal in ROOT.glob('recover-*/status.json'):
            if json.loads(journal.read_text())['phase'] not in ('complete', 'complete-old-container-retained', 'rolled-back'):
                raise RuntimeError('Unfinished recovery; inspect ' + str(journal))
        folder = Path(tempfile.mkdtemp(prefix='recover-', dir=ROOT))
        archive = folder / 'vault.tar.zst'
        raw = folder / 'vault.tar'
        try:
            with open(archive, 'xb') as output:
                shutil.copyfileobj(sys.stdin.buffer, output)
            report(55)
            run(['zstd', '-q', '-t', str(archive)], 300)
            run(['zstd', '-q', '-d', str(archive), '-o', str(raw)], 300)
            image = unpack(raw, folder)
            report(65)
            run(['systemctl', 'start', 'docker'], 60)
            # Disconnects must not interrupt the transactional replacement.
            for sig in (signal.SIGHUP, signal.SIGINT, signal.SIGTERM):
                signal.signal(sig, signal.SIG_IGN)
            previous = restore(folder / 'bitwarden', image, folder)
            print('RECOVER_OK ' + str(previous), flush=True)
        finally:
            archive.unlink(missing_ok=True)
            raw.unlink(missing_ok=True)
            if not (folder / 'status.json').exists():
                shutil.rmtree(folder)


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(str(error) if isinstance(error, RuntimeError) else type(error).__name__, file=sys.stderr)
        sys.exit(1)
