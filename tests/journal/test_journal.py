"""Exercise the real journal binary with isolated homes and deterministic editors."""
import os
from pathlib import Path
import stat
import subprocess
import tempfile
import time
import unittest

BIN = Path(os.environ.get('TAM_JOURNAL_TEST_BIN', Path(__file__).resolve().parents[2] / 'bin/tam-journal')).resolve()


class JournalTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.home = self.root / 'home'
        self.home.mkdir()
        self.journal = self.home / 'Journal'
        self.editor = self.root / 'micro'
        self.editor.write_text('''#!/usr/bin/python3
import os,sys,time
from pathlib import Path
p=Path(sys.argv[1])
if 'READY' in os.environ:
 Path(os.environ['READY']).touch()
 while not Path(os.environ['GO']).exists():time.sleep(.01)
if 'TEXT' in os.environ:p.write_text(os.environ['TEXT'])
if 'OUTSIDE' in os.environ:Path(os.environ['OUTSIDE']).write_text('outside edit')
if 'BLOCK_SAVE' in os.environ:Path(os.environ['BLOCK_SAVE']).mkdir()
sys.exit(int(os.environ.get('EDITOR_EXIT','0')))
''')
        self.editor.chmod(0o755)
        self.env = {'HOME': str(self.home), 'PATH': str(self.root) + ':/usr/bin', 'TZ': 'UTC', 'LANG': 'C.UTF-8'}
        self.day = self.journal / '2026-11-11.txt'

    def tearDown(self):
        self.tmp.cleanup()

    def run_journal(self, *args, **env):
        return subprocess.run([str(BIN), *args], env={**self.env, **env}, text=True, capture_output=True, timeout=10)

    def drafts(self):
        return list((self.journal / '.drafts').glob('*'))

    def test_invalid_dates_and_options_have_no_side_effect(self):
        for args in [('--date','2026-02-29'),('--date','2026-13-01'),('--date','../escape'),('--date','0000-01-01'),('--date','2026-1-1'),('--date',),('other',),('add','add')]:
            self.assertEqual(self.run_journal(*args).returncode, 2, args)
            self.assertFalse(self.journal.exists())

    def test_leap_day_and_unicode_are_saved_as_plain_text(self):
        result = self.run_journal('add', '--date', '2024-02-29', TEXT='Café — 日本語\nsecond line')
        self.assertEqual(result.returncode, 0, result.stderr)
        text = (self.journal / '2024-02-29.txt').read_text()
        self.assertIn('Café — 日本語\nsecond line\n', text)
        self.assertRegex(text, r'\[\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\+0000\]')

    def test_blank_capture_and_unchanged_open_cancel(self):
        for args in [('add','--date','2026-11-11'),('--date','2026-11-11')]:
            result = self.run_journal(*args)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse(self.day.exists())
            self.assertFalse(self.drafts())

    def test_append_and_full_edit_preserve_exact_text(self):
        for note in ('first', 'second'):
            result = self.run_journal('add','--date','2026-11-11',TEXT=note)
            self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('first\n', self.day.read_text())
        self.assertIn('second\n', self.day.read_text())
        self.assertEqual(self.run_journal('--date','2026-11-11',TEXT='edited\n').returncode, 0)
        self.assertEqual(self.day.read_text(), 'edited\n')

    def test_private_modes_and_custom_directory(self):
        custom = self.root / 'private' / 'notes'
        result = self.run_journal('add','--date','2026-11-11',TEXT='private',TAM_JOURNAL_DIR=str(custom))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(stat.S_IMODE(custom.stat().st_mode), 0o700)
        self.assertEqual(stat.S_IMODE((custom/'2026-11-11.txt').stat().st_mode), 0o600)
        self.assertFalse(self.journal.exists())

    def test_shared_root_and_symlink_root_are_refused(self):
        self.journal.mkdir(mode=0o755)
        self.assertNotEqual(self.run_journal('add',TEXT='x').returncode, 0)
        self.journal.rmdir()
        self.journal.symlink_to(self.root, target_is_directory=True)
        self.assertNotEqual(self.run_journal('add',TEXT='x').returncode, 0)

    def test_symlink_day_never_changes_target(self):
        self.journal.mkdir(mode=0o700)
        target = self.root/'precious'; target.write_text('keep')
        self.day.symlink_to(target)
        result = self.run_journal('add','--date','2026-11-11',TEXT='new')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(target.read_text(), 'keep')
        self.assertEqual(self.drafts()[0].read_text(), 'new')

    def test_failed_editor_preserves_draft_without_touching_day(self):
        result = self.run_journal('add','--date','2026-11-11',TEXT='saved draft',EDITOR_EXIT='1')
        self.assertEqual(result.returncode, 1)
        self.assertIn('Draft retained:', result.stderr)
        self.assertFalse(self.day.exists())
        self.assertEqual(self.drafts()[0].read_text(), 'saved draft')

    def test_external_change_is_not_overwritten(self):
        self.run_journal('add','--date','2026-11-11',TEXT='before')
        result = self.run_journal('--date','2026-11-11',TEXT='my edit',OUTSIDE=str(self.day))
        self.assertEqual(result.returncode, 1)
        self.assertEqual(self.day.read_text(), 'outside edit')
        self.assertEqual(self.drafts()[0].read_text(), 'my edit')

    def test_save_failure_preserves_saved_draft(self):
        result = self.run_journal('add','--date','2026-11-11',TEXT='retained',BLOCK_SAVE=str(self.day))
        self.assertEqual(result.returncode, 1)
        self.assertEqual(self.drafts()[0].read_text(), 'retained')

    def test_busy_day_retains_quick_note(self):
        ready, go = self.root/'ready', self.root/'go'
        process = subprocess.Popen([str(BIN),'--date','2026-11-11'],env={**self.env,'READY':str(ready),'GO':str(go),'TEXT':'long entry'},stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        try:
            end = time.monotonic()+5
            while not ready.exists() and time.monotonic()<end: time.sleep(.01)
            self.assertTrue(ready.exists())
            result = self.run_journal('add','--date','2026-11-11',TEXT='quick note')
            self.assertEqual(result.returncode, 3, result.stderr)
            self.assertTrue(any(p.read_text()=='quick note' for p in self.drafts()))
        finally:
            go.touch(); out,err = process.communicate(timeout=5)
        self.assertEqual(process.returncode, 0, err)
        self.assertEqual(self.day.read_text(), 'long entry')

    def test_invalid_utf8_in_existing_day_is_refused(self):
        self.journal.mkdir(mode=0o700)
        self.day.write_bytes(b'\xffbad')
        result = self.run_journal('--date','2026-11-11',TEXT='replacement')
        self.assertEqual(result.returncode, 1)
        self.assertEqual(self.day.read_bytes(), b'\xffbad')

    def test_daily_default_is_fixed_when_capture_starts(self):
        expected = time.strftime('%Y-%m-%d', time.gmtime())
        result = self.run_journal('add',TEXT='today')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('today', (self.journal/(expected+'.txt')).read_text())

    def test_missing_editor_retains_draft(self):
        self.editor.unlink()
        result = self.run_journal('add','--date','2026-11-11',PATH=str(self.root))
        self.assertEqual(result.returncode, 1)
        self.assertTrue(self.drafts())


if __name__ == '__main__':
    unittest.main()
