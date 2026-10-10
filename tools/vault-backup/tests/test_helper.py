#!/usr/bin/env python3
"""Exercise resume paths with a fake Docker runner; never touches production Docker."""
import importlib.machinery
import importlib.util
import io
import json
import os
from pathlib import Path
import signal
import sqlite3
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

loader = importlib.machinery.SourceFileLoader('helper', sys.argv[1])
spec = importlib.util.spec_from_loader(loader.name, loader)
helper = importlib.util.module_from_spec(spec)
loader.exec_module(helper)
sys.argv = [sys.argv[1]]

class DiagnosticTests(unittest.TestCase):
    def test_unavailable_daemon(self):
        for diagnostic in (
                b'Cannot connect to the Docker daemon at unix:///var/run/docker.sock',
                b'failed to connect to the docker API at unix:///var/run/docker.sock'):
            error = subprocess.CalledProcessError(1, ['docker', 'inspect', 'bitwarden'],
                                                 stderr=diagnostic)
            with patch.object(helper.subprocess, 'run', side_effect=error):
                with self.assertRaisesRegex(RuntimeError, 'sudo systemctl start docker'):
                    helper.run(['docker', 'inspect', 'bitwarden'])

    def test_failure_does_not_expose_secrets(self):
        for args, action in ((['docker', 'inspect', 'bitwarden'], 'Docker container inspection'),
                             (['tar', '-cf', 'secret-path'], 'Snapshot creation'),
                             (['zstd', '-q', '-19', 'secret-path', '-o', 'output'], 'Archive compression'),
                             (['zstd', '-q', '-t', 'secret-path'], 'Archive validation')):
            error = subprocess.CalledProcessError(2, args, output=b'secret-output',
                                                 stderr=b'secret-stderr')
            with patch.object(helper.subprocess, 'run', side_effect=error):
                with self.assertRaises(RuntimeError) as raised:
                    helper.run(args)
            self.assertIn(action, str(raised.exception))
            self.assertIn('exit code 2', str(raised.exception))
            self.assertNotIn('secret', str(raised.exception))

    def test_timeout(self):
        error = subprocess.TimeoutExpired(['docker', 'pause', 'bitwarden'], 30,
                                          stderr=b'secret-stderr')
        with patch.object(helper.subprocess, 'run', side_effect=error):
            with self.assertRaisesRegex(RuntimeError, 'Container pause timed out after 30 seconds'):
                helper.run(['docker', 'pause', 'bitwarden'], timeout=30)

class ResumeTests(unittest.TestCase):
    def scenario(self, fault=None):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            base, parent = root / 'bitwarden', root / 'backups'
            (base / 'data').mkdir(parents=True)
            parent.mkdir()
            with sqlite3.connect(base / 'data/vault.db') as db:
                db.execute('CREATE TABLE test (id INTEGER)')
            for name in ('settings.env', 'compose.yaml'):
                (base / name).write_text('test fixture')
            state = {'Running': True, 'Paused': False}
            events = []
            retries = 0
            def fake_run(args, **kwargs):
                nonlocal retries
                events.append(args[0:2])
                if args[:2] == ['docker', 'inspect']:
                    info = dict(State=state.copy(), Mounts=[dict(Destination='/etc/bitwarden',Source=str(base/'data'))],Config={'Image':'test'})
                    data = state if '--format' in args else [info]
                    return subprocess.CompletedProcess(args,0,json.dumps(data).encode(),b'')
                if args[:2] == ['docker', 'pause']:
                    state['Paused'] = True
                    if fault == 'ambiguous_pause':
                        raise RuntimeError('Injected pause failure')
                elif args[:2] == ['docker', 'unpause']:
                    retries += 1
                    if fault == 'resume_retry' and retries == 1:
                        raise RuntimeError('Injected transient resume failure')
                    state['Paused'] = False
                elif args[0] == 'tar':
                    if args[1] == '-cf':
                        if fault == 'tar':
                            raise RuntimeError('Injected snapshot failure')
                        if fault == 'signal':
                            helper.interrupted(signal.SIGTERM, None)
                        with tarfile.open(args[2], 'w') as archive:
                            for name in ('data','settings.env','compose.yaml'):
                                archive.add(base/name, arcname='bitwarden/'+name)
                    else:
                        with tarfile.open(args[2], 'a') as archive:
                            archive.add(Path(args[4])/'manifest.json', arcname='manifest.json')
                elif args[0] == 'zstd':
                    assert not state['Paused'], 'Compression started before resume'
                    if '-o' in args:
                        Path(args[-1]).write_bytes(b'test compressed stream')
                return subprocess.CompletedProcess(args,0,b'',b'')
            actual_open = open
            def mapped_open(path, *args, **kwargs):
                if str(path) == '/run/vault-backup.lock':
                    path = root / 'lock'
                return actual_open(path, *args, **kwargs)
            def mapped_path(path):
                return {'/opt/bitwarden':base, '/var/backups/bitwarden':parent}.get(str(path),Path(path))
            previous_handlers = {s: signal.getsignal(s) for s in (signal.SIGTERM,signal.SIGHUP,signal.SIGINT)}
            try:
                with patch.object(helper, 'Path', side_effect=mapped_path), patch.object(helper, 'run', side_effect=fake_run), patch.object(helper.os,'geteuid',return_value=0), patch.object(helper.shutil,'which',return_value='/usr/bin/mock'), patch('builtins.open',side_effect=mapped_open), patch.object(helper.sys,'stdout',io.TextIOWrapper(io.BytesIO())):
                    if fault in ('tar','signal','ambiguous_pause'):
                        with self.assertRaises(RuntimeError):
                            helper.main()
                    else:
                        helper.main()
                self.assertFalse(state['Paused'])
                self.assertTrue(state['Running'])
                self.assertEqual(list(parent.iterdir()), [])
                self.assertIn(['docker','unpause'], events)
                if fault == 'resume_retry':
                    self.assertEqual(retries, 2)
            finally:
                for sig, handler in previous_handlers.items():
                    signal.signal(sig, handler)
    def test_success(self): self.scenario()
    def test_snapshot_failure(self): self.scenario('tar')
    def test_ambiguous_pause_failure(self): self.scenario('ambiguous_pause')
    def test_signal_during_snapshot(self): self.scenario('signal')
    def test_transient_resume_failure(self): self.scenario('resume_retry')

if __name__ == '__main__':
    unittest.main()
