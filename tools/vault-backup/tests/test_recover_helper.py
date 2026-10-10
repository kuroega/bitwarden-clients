import importlib.util
import io
import json
from pathlib import Path
import sqlite3
import subprocess
from types import SimpleNamespace
import tarfile
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('recover', Path(__file__).parents[1] / 'vm/vault-recover.py')
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)
IMAGE = 'ghcr.io/bitwarden/lite@sha256:' + 'a' * 64


class RecoveryTests(unittest.TestCase):
    def test_server_url_configuration(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / 'server.local.ini'
            metadata = SimpleNamespace(st_uid=0, st_mode=0o100600)
            with patch.object(Path, 'lstat', return_value=metadata):
                config.write_text('[server]\nvault_url=https://vault.example.invalid\n')
                self.assertEqual(helper.load_server_url(config), 'https://vault.example.invalid')
                for url in ('http://vault.example.invalid', 'https://user@vault.example.invalid',
                            'https://vault.example.invalid/path', 'https://vault.example.invalid?x=1'):
                    config.write_text('[server]\nvault_url=' + url + '\n')
                    with self.assertRaisesRegex(RuntimeError, 'Missing or invalid'):
                        helper.load_server_url(config)
            with self.assertRaisesRegex(RuntimeError, 'Missing or invalid'):
                helper.load_server_url(Path(directory) / 'missing.ini')
            with patch.object(Path, 'lstat', return_value=SimpleNamespace(st_uid=0, st_mode=0o100666)):
                with self.assertRaisesRegex(RuntimeError, 'Missing or invalid'):
                    helper.load_server_url(config)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.base = self.root / 'bitwarden'
        self.base.mkdir()
        (self.base / 'data').mkdir()
        (self.base / 'original').write_text('previous deployment')
        self.folder = self.root / 'recover-test'
        self.folder.mkdir()
        self.deployment = self.folder / 'bitwarden'
        self.deployment.mkdir()
        (self.deployment / 'data').mkdir()
        db = sqlite3.connect(self.deployment / 'data/vault.db')
        db.execute('CREATE TABLE fixture (value TEXT)')
        db.execute("INSERT INTO fixture VALUES ('restored')")
        db.commit()
        db.close()
        for name in ('compose.yaml', 'settings.env'):
            (self.deployment / name).write_text('fixture')
        (self.folder / 'manifest.json').write_text(json.dumps({'format': 1, 'container': 'bitwarden', 'image': IMAGE}))
        self.calls = []
        self.old = True
        self.running = True
        self.paused = False
        self.fail = None
        self.base_patch = patch.object(helper, 'BASE', self.base)
        self.base_patch.start()
        self.root_patch = patch.object(helper, 'ROOT', self.root)
        self.root_patch.start()

    def tearDown(self):
        self.base_patch.stop()
        self.root_patch.stop()
        self.temp.cleanup()

    def command(self, args, timeout=120):
        self.calls.append(args)
        if self.fail and args[:2] == self.fail:
            raise RuntimeError('fixture command failure')
        if args[:2] == ['docker', 'ps']:
            return b'{"Names":"bitwarden"}' if self.old else b''
        if args[:2] == ['docker', 'inspect']:
            return json.dumps([{'Mounts': [{'Source': str(self.base / 'data'), 'Destination': '/etc/bitwarden'}],
                                'State': {'Running': self.running, 'Paused': self.paused}}]).encode()
        return b''

    def restore(self, healthy=True):
        check = None if healthy else RuntimeError('fixture health failure')
        with patch.object(helper, 'run', self.command), patch.object(helper, 'healthy', side_effect=check), patch.object(helper.os, 'chown'):
            return helper.restore(self.deployment, IMAGE, self.folder)

    def test_success_retains_old_data(self):
        previous = self.restore()
        self.assertEqual((previous / 'original').read_text(), 'previous deployment')
        self.assertTrue((self.base / 'data/vault.db').exists())
        self.assertEqual(json.loads((self.folder / 'status.json').read_text())['phase'], 'complete')
        create = next(call for call in self.calls if call[:2] == ['docker', 'create'])
        self.assertIn('127.0.0.1:8080:8080', create)
        self.assertIn(IMAGE, create)
        self.assertTrue(any(call[:2] == ['tailscale', 'serve'] for call in self.calls))

    def test_health_failure_rolls_back(self):
        with self.assertRaisesRegex(RuntimeError, 'previous deployment restored'):
            self.restore(False)
        self.assertEqual((self.base / 'original').read_text(), 'previous deployment')
        self.assertTrue((self.folder / 'failed-deployment/data/vault.db').exists())
        self.assertEqual(json.loads((self.folder / 'status.json').read_text())['phase'], 'rolled-back')

    def test_create_failure_rolls_back(self):
        self.fail = ['docker', 'create']
        with self.assertRaisesRegex(RuntimeError, 'previous deployment restored'):
            self.restore()
        self.assertTrue((self.base / 'original').exists())

    def test_image_failure_does_not_stop_service(self):
        def fail(args, timeout=120):
            self.calls.append(args)
            raise RuntimeError('image unavailable')
        with patch.object(helper, 'run', fail), self.assertRaises(RuntimeError):
            helper.restore(self.deployment, IMAGE, self.folder)
        self.assertTrue((self.base / 'original').exists())
        self.assertFalse(any(call[:2] == ['docker', 'stop'] for call in self.calls))

    def test_missing_container(self):
        self.old = False
        self.restore()
        self.assertFalse(any(call[:2] == ['docker', 'stop'] for call in self.calls))

    def test_stopped_old_container_stays_stopped_on_rollback(self):
        self.running = False
        with self.assertRaises(RuntimeError):
            self.restore(False)
        self.assertEqual(sum(call[:2] == ['docker', 'start'] for call in self.calls), 1)

    def test_paused_old_container_restored_on_rollback(self):
        self.paused = True
        with self.assertRaises(RuntimeError):
            self.restore(False)
        self.assertIn(['docker', 'unpause', 'bitwarden'], self.calls)
        self.assertIn(['docker', 'pause', 'bitwarden'], self.calls)

    def test_rollback_failure_is_explicit(self):
        self.fail = ['docker', 'rm']
        with self.assertRaisesRegex(RuntimeError, 'ROLLBACK FAILED'):
            self.restore(False)
        self.assertEqual(json.loads((self.folder / 'status.json').read_text())['phase'], 'rollback-failed')

    def test_old_container_cleanup_failure_keeps_recovered_vault(self):
        self.fail = ['docker', 'rm']
        previous = self.restore()
        self.assertTrue((previous / 'original').exists())
        self.assertTrue((self.base / 'data/vault.db').exists())
        self.assertEqual(json.loads((self.folder / 'status.json').read_text())['phase'], 'complete-old-container-retained')

    def archive(self):
        path = self.root / 'snapshot.tar'
        with tarfile.open(path, 'w') as tar:
            tar.add(self.deployment, arcname='bitwarden')
            tar.add(self.folder / 'manifest.json', arcname='manifest.json')
        return path

    def test_safe_archive_and_sqlite(self):
        path = self.archive()
        target = self.root / 'extracted'
        target.mkdir()
        self.assertEqual(helper.unpack(path, target), IMAGE)

    def test_reject_unsafe_members(self):
        for name, kind in [('../escape', tarfile.REGTYPE), ('/etc/passwd', tarfile.REGTYPE),
                           ('bitwarden/data/link', tarfile.SYMTYPE), ('bitwarden/data/link', tarfile.LNKTYPE),
                           ('bitwarden/data/device', tarfile.CHRTYPE), ('unexpected', tarfile.REGTYPE)]:
            path = self.root / 'unsafe.tar'
            with tarfile.open(path, 'w') as tar:
                member = tarfile.TarInfo(name)
                member.type = kind
                member.linkname = '/etc/passwd'
                tar.addfile(member, io.BytesIO())
            with self.assertRaises(RuntimeError):
                helper.unpack(path, self.root)

    def test_corrupt_database(self):
        (self.deployment / 'data/vault.db').write_bytes(b'not sqlite')
        target = self.root / 'corrupt'
        target.mkdir()
        with self.assertRaises(sqlite3.DatabaseError):
            helper.unpack(self.archive(), target)

    def test_unpinned_image_rejected(self):
        (self.folder / 'manifest.json').write_text(json.dumps({'format': 1, 'container': 'bitwarden', 'image': 'untrusted:latest'}))
        target = self.root / 'unpinned'
        target.mkdir()
        with self.assertRaises(RuntimeError):
            helper.unpack(self.archive(), target)

    def pipeline(self, payload):
        real_run = helper.run
        def command(args, timeout=120):
            return real_run(args, timeout) if args[0] == 'zstd' else self.command(args, timeout)
        with patch.object(helper, 'load_server_url', return_value='https://vault.example.invalid'), \
                patch.object(helper, 'LOCK', self.root / 'lock'), patch.object(helper, 'run', command), \
                patch.object(helper, 'healthy'), patch.object(helper.os, 'chown'), \
                patch.object(helper.os, 'geteuid', return_value=0), patch.object(helper.signal, 'signal'), \
                patch.object(helper.sys, 'argv', ['vault-recover']), \
                patch.object(helper.sys, 'stdin', SimpleNamespace(buffer=io.BytesIO(payload))), \
                patch.dict(helper.os.environ), patch.object(helper, 'report'):
            helper.main()

    def test_complete_stdin_zstd_sqlite_pipeline(self):
        payload = subprocess.run(['zstd', '-q', '-c', str(self.archive())], check=True, stdout=subprocess.PIPE).stdout
        self.pipeline(payload)
        self.assertTrue((self.base / 'data/vault.db').is_file())
        journals = list(self.root.glob('recover-*/status.json'))
        self.assertEqual(len(journals), 1)
        self.assertEqual(json.loads(journals[0].read_text())['phase'], 'complete')
        self.assertFalse(list(self.root.glob('recover-*/vault.tar*')))

    def test_truncated_stdin_cannot_touch_service(self):
        with self.assertRaises(RuntimeError):
            self.pipeline(b'\x28\xb5\x2f\xfd\x00')
        self.assertEqual(self.calls, [])
        self.assertTrue((self.base / 'original').exists())

    def test_unfinished_journal_blocks_retry(self):
        (self.folder / 'status.json').write_text(json.dumps({'phase': 'data-installed'}))
        with self.assertRaisesRegex(RuntimeError, 'Unfinished recovery'):
            self.pipeline(b'')
        self.assertEqual(self.calls, [])


if __name__ == '__main__':
    unittest.main()
