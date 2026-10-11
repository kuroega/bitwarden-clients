#!/usr/bin/env python3
"""Enable native Docker/Bitwarden boot startup without recreating the container."""
import fcntl
import json
import os
from pathlib import Path
import subprocess
import tempfile


def run(args):
    return subprocess.run(args, check=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, timeout=60).stdout


def main():
    if os.geteuid() != 0:
        raise RuntimeError('Run as root')
    os.umask(0o077)
    with open('/run/vault-backup.lock', 'a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        info = json.loads(run(['docker', 'inspect', 'bitwarden']))[0]
        if not info['State']['Running'] or info['State']['Paused']:
            raise RuntimeError('Bitwarden must be running and unpaused before configuration')
        if not any(m['Source'] == '/opt/bitwarden/data' and
                   m['Destination'] == '/etc/bitwarden' for m in info['Mounts']):
            raise RuntimeError('Unexpected persistent data mount')
        compose = Path('/opt/bitwarden/compose.yaml')
        if compose.is_symlink() or compose.stat().st_uid != 0:
            raise RuntimeError('Unexpected Compose file owner/type')
        original = compose.read_bytes()
        text = original.decode('utf-8')
        old, new = '    restart: "no"', '    restart: always'
        if text.count(old) == 1:
            text = text.replace(old, new)
        elif text.count(new) != 1:
            raise RuntimeError('Unexpected Compose restart policy; refusing modification')
        with tempfile.NamedTemporaryFile(dir=compose.parent, suffix='.yaml', delete=False) as file:
            candidate = Path(file.name)
            file.write(text.encode('utf-8'))
            file.flush()
            os.fsync(file.fileno())
        try:
            run(['docker', 'compose', '-f', str(candidate), 'config', '--quiet'])
            backup = compose.with_name('compose.yaml.before-autostart')
            if not backup.exists():
                with backup.open('xb') as file:
                    file.write(original)
                    file.flush()
                    os.fsync(file.fileno())
            os.chmod(candidate, 0o600)
            os.replace(candidate, compose)
            descriptor = os.open(compose.parent, os.O_RDONLY | os.O_DIRECTORY)
            try:
                os.fsync(descriptor)
            finally:
                os.close(descriptor)
            run(['docker', 'update', '--restart', 'always', 'bitwarden'])
            run(['systemctl', 'enable', '--now', 'containerd.service', 'docker.service', 'docker.socket'])
        finally:
            candidate.unlink(missing_ok=True)
        state = json.loads(run(['docker', 'inspect', 'bitwarden']))[0]
        assert state['Id'] == info['Id'], 'Container unexpectedly replaced'
        assert state['HostConfig']['RestartPolicy']['Name'] == 'always'
        assert state['State']['Running'] and not state['State']['Paused']
        print('Docker boot startup enabled; Bitwarden restart=always; existing container preserved.')


if __name__ == '__main__':
    try:
        main()
    except subprocess.SubprocessError:
        raise SystemExit('Autostart configuration command failed; inspect service state and retry.')
