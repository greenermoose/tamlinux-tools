#!/usr/bin/env python3
"""Format 2 tests: temporary roots and mandatory private PID namespace."""
from contextlib import contextmanager
import hashlib
import io
import os
from pathlib import Path
import random
import shutil
import signal
import subprocess
import tarfile
import tempfile
import time
import unittest
from pid_namespace import enter_namespace, require_namespace

BIN = Path(os.environ.get('TAM_BUNDLE_BIN', Path(__file__).resolve().parents[2] / 'bin/tam-bundle-test')).resolve()

def setUpModule():
    require_namespace()

class ProductTransactions(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='tam-bundle-v2-', dir=os.environ.get('TAM_TEST_DIR', '/tmp'))
        self.base = Path(self.temp.name)
        self.root = self.base / 'root'
        p = self.root / 'usr/share/tamlinux/rootstock'
        p.mkdir(parents=True)
        (p / 'profile').write_text('format 1\nallow usr/bin\nallow usr/share/man\n')
        self.a = self.bundle('A')
        self.b = self.bundle('B')

    def tearDown(self):
        self.temp.cleanup()

    def run_tb(self, *args, code=0, env=None):
        r = subprocess.run([str(BIN), *map(str, args)], capture_output=True, text=True,
                           env=dict(os.environ, **(env or {})), timeout=30)
        self.assertIn(r.returncode, (code,) if isinstance(code, int) else code,
                      f'{args}: {r.returncode}\n{r.stdout}\n{r.stderr}')
        self.assertNotIn('ERROR: AddressSanitizer', r.stderr)
        self.assertNotIn('runtime error:', r.stderr)
        return r

    def tb(self, *args, **kw):
        return self.run_tb('--root', self.root, *args, **kw)

    def bundle(self, label, extra=False):
        b = self.base / label
        (b / 'tree/share').mkdir(parents=True)
        (b / 'placed/usr/bin').mkdir(parents=True)
        (b / 'tree/share/value').write_text(label)
        (b / 'tree/share/loop.py').write_text('import time\nprint("ready", flush=True)\ntime.sleep(60)\n')
        (b / 'placed/usr/bin/tam-demo').write_text('#!/bin/sh\necho ' + label + '\n')
        (b / 'placed/usr/bin/tam-demo').chmod(0o755)
        if extra:
            shutil.copy2('/usr/bin/python3', b/'placed/usr/bin/tam-demo')
        if label == 'A':
            (b / 'placed/usr/bin/tam-old').write_text('old')
        self.run_tb('seal', b, '--format', '2', '--name', 'demo', '--version', label,
                    '--target', 'arch x86_64 baseline x86-64-v2')
        return b

    def id(self, bundle):
        fields = dict(line.split(' ', 1) for line in (bundle/'BUNDLE').read_text().splitlines())
        return f"{fields['name']}-{fields['version']}-{fields['hash'][7:19]}"

    def tx(self):
        p = self.root / 'var/lib/tamlinux/transaction'
        return dict(line.split(' ', 1) for line in p.read_text().splitlines()) if p.exists() else None

    def install_a(self):
        self.tb('install', self.a)
        self.tb('apply')
        self.assertEqual(self.tx()['state'], 'complete')
        self.tb('check')

    def assert_live(self, bundle):
        self.tb('check')
        self.assertEqual((self.root/'usr/lib/tamlinux/current').readlink().as_posix(), 'version/'+self.id(bundle))
        self.assertEqual((self.root/'usr/bin/tam-demo').read_bytes(), (bundle/'placed/usr/bin/tam-demo').read_bytes())

    def test_prepare_apply_cancel_and_identities(self):
        self.install_a()
        self.tb('install', self.b)
        first = self.tx()['id']
        self.assertEqual(self.tx()['state'], 'ready')
        self.assertIn('echo A', (self.root/'usr/bin/tam-demo').read_text())
        self.tb('status', code=4)
        self.tb('cancel')
        self.assertIsNone(self.tx())
        self.assert_live(self.a)
        self.tb('install', self.b)
        self.assertNotEqual(self.tx()['id'], first)
        self.tb('apply')
        self.assert_live(self.b)
        self.assertFalse((self.root/'usr/bin/tam-old').exists())
        self.tb('cancel', code=4)
        self.tb('apply')  # archives a complete transaction
        self.assertIsNone(self.tx())

    def test_bundle_hash_settings_and_format_one(self):
        self.run_tb('verify', self.a)
        (self.a/'SETTINGS').write_text('format 1\ncommon .bashrc\n')
        self.run_tb('verify', self.a, code=1)
        legacy=self.base/'legacy';legacy.mkdir();(legacy/'data').write_text('v1')
        self.run_tb('seal', legacy, '--name', 'legacy', '--version', '1')
        self.run_tb('verify', legacy)
        self.tb('install', legacy, code=1)

    def test_archive_portable_and_unsafe(self):
        archive=self.base/'bundle.tbundle'
        self.run_tb('pack', self.a, '-o', archive)
        self.tb('install', archive)
        self.tb('apply')
        self.assert_live(self.a)
        evil=self.base/'evil.tbundle'
        with tarfile.open(evil,'w',format=tarfile.PAX_FORMAT) as t:
            info=tarfile.TarInfo('../escape');info.size=3;t.addfile(info,io.BytesIO(b'bad'))
        self.tb('install',evil,code=1)
        self.assertFalse((self.base/'escape').exists())
        self.assert_live(self.a)

    def test_unowned_collision_and_symlink_parent(self):
        p=self.root/'usr/bin';p.mkdir();(p/'tam-demo').write_text('foreign')
        self.tb('install',self.a,code=1)
        self.assertEqual((p/'tam-demo').read_text(),'foreign')
        shutil.rmtree(p);p.symlink_to(self.base, target_is_directory=True)
        self.tb('install',self.a,code=1)
        self.assertFalse((self.base/'tam-demo').exists())

    def test_recheck_and_repair_rollback_copy(self):
        self.install_a();self.tb('install',self.b)
        backup=self.root/'usr/lib/tamlinux/version'/self.id(self.a)/'.placed/usr/bin/tam-demo'
        backup.write_text('corrupted')
        self.tb('apply');self.assert_live(self.b)
        self.assertEqual(backup.read_bytes(),(self.a/'placed/usr/bin/tam-demo').read_bytes())

    def test_live_change_after_prepare_refuses(self):
        self.install_a();self.tb('install',self.b)
        (self.root/'usr/bin/tam-demo').write_text('changed')
        self.tb('apply',code=1)
        self.assertEqual(self.tx()['state'],'ready')
        self.assertEqual((self.root/'usr/bin/tam-demo').read_text(),'changed')

    def test_busy_interpreter_and_launcher_exemption(self):
        self.install_a();self.tb('install',self.b)
        script=self.root/'usr/lib/tamlinux/current/share/loop.py'
        p=subprocess.Popen(['/usr/bin/python3',str(script)], stdout=subprocess.PIPE)
        try:
            import select
            self.assertTrue(select.select([p.stdout], [], [], 5)[0], 'interpreter did not become ready')
            self.assertEqual(p.stdout.readline(), b'ready\n')
            self.tb('apply')
            self.assertEqual(self.tx()['state'],'ready')
            self.tb('apply','--yes');p.wait(timeout=6)
            self.assert_live(self.b)
        finally:
            if p.poll() is None:p.kill();p.wait()
            p.stdout.close()

    @contextmanager
    def sleeper(self, *, executable=None, cwd=None, argument=None, hold=None):
        # Readiness comes over a pipe. All possible signal recipients are
        # test children created inside the mandatory PID namespace.
        read, write = os.pipe()
        code = ('import mmap,os,time; '
                'f=open(os.environ["TB_HOLD"], "rb") if os.environ.get("TB_HOLD") else None; '
                'mapping=mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) '
                'if f and os.environ.get("TB_MAP") else None; '
                'f.close() if mapping else None; '
                f'os.write({write}, b"ready"); os.close({write}); time.sleep(60)')
        args = [str(executable or '/usr/bin/python3'), '-c', code]
        if argument is not None:
            args.append(str(argument))
        env = dict(os.environ)
        if hold is not None:
            env['TB_HOLD'] = str(hold[0])
            if hold[1] == 'map':
                env['TB_MAP'] = '1'
        try:
            p = subprocess.Popen(args, cwd=cwd, env=env, pass_fds=(write,))
        finally:
            os.close(write)
        try:
            import select
            self.assertTrue(select.select([read], [], [], 5)[0], 'helper did not become ready')
            self.assertEqual(os.read(read, 5), b'ready')
            yield p
        finally:
            os.close(read)
            if p.poll() is None:
                p.kill()
            p.wait(timeout=6)

    def test_busy_does_not_select_names_or_path_substrings(self):
        from contextlib import ExitStack
        self.install_a(); self.tb('install', self.b)
        named = self.base/'Hyprland'
        shutil.copy2('/usr/bin/python3', named)
        placed = self.root/'usr/bin/tam-demo'
        arguments = [self.base/'prefix'/str(placed).lstrip('/'),
                     self.root/'usr/lib/tamlinux/current-other/file',
                     Path(str(placed)+'.backup'), str(placed)+'/child',
                     'unrelated '+str(placed)+' text']
        with ExitStack() as stack:
            processes = [stack.enter_context(self.sleeper(executable=named))]
            processes += [stack.enter_context(self.sleeper(argument=arg)) for arg in arguments]
            self.tb('apply', '--yes')
            self.assert_live(self.b)
            for p in processes:
                self.assertIsNone(p.poll(), 'unrelated process was signalled')

    def test_busy_paths_with_spaces_fds_maps_and_cwd(self):
        spaced = self.base/'root with spaces'
        self.root.rename(spaced); self.root = spaced
        for kind in ('argument', 'cwd', 'fd', 'map'):
            with self.subTest(kind=kind):
                self.install_a(); self.tb('install', self.b)
                old = self.root/'usr/lib/tamlinux/version'/self.id(self.a)
                path = old/'share/file with spaces'
                if kind == 'argument':
                    options = {'argument': old/'share/value'}
                elif kind == 'cwd':
                    options = {'cwd': old/'share'}
                else:
                    path.write_text('data')
                    options = {'hold': (path, kind)}
                with self.sleeper(**options) as p:
                    if kind in ('fd', 'map'):
                        path.unlink()  # /proc adds its " (deleted)" suffix
                    self.tb('apply')
                    self.assertEqual(self.tx()['state'], 'ready')
                    self.tb('apply', '--yes')
                    self.assertEqual(p.wait(timeout=6), -signal.SIGTERM)
                self.assert_live(self.b)

    def test_busy_owned_placed_executable(self):
        self.a = self.bundle('executable', extra=True)
        self.install_a(); self.tb('install', self.b)
        with self.sleeper(executable=self.root/'usr/bin/tam-demo') as p:
            self.tb('apply')
            self.assertEqual(self.tx()['state'], 'ready')
            self.tb('apply', '--yes')
            self.assertEqual(p.wait(timeout=6), -signal.SIGTERM)
        self.assert_live(self.b)

    def test_live_root_graphical_guard_never_signals(self):
        named = self.base/'Hyprland'
        shutil.copy2('/usr/bin/python3', named)
        driver = BIN.with_name('tam-bundle-busy-test')
        with self.sleeper(executable=named) as p, self.sleeper(argument='/usr/lib/tamlinux/current/share/fixture') as client:
            for root in ('/', '/.'):
                for old in ('fixture-old-version', '-'):
                    r = subprocess.run([str(driver), root, old], capture_output=True, text=True, timeout=10)
                    self.assertEqual(r.returncode, 0, r.stdout+r.stderr)
                    self.assertIn('graphical session still running', r.stdout)
                    self.assertIsNone(p.poll(), 'graphical blocker was signalled')
                    self.assertIsNone(client.poll(), 'a live session client was signalled')
        # A command launched from a graphical terminal can have Hyprland
        # in its ancestor chain. The launcher exemption must not hide it.
        with self.sleeper(argument='/usr/lib/tamlinux/current/share/fixture') as client:
            launcher = 'import subprocess,sys; sys.exit(subprocess.call(sys.argv[1:]))'
            r = subprocess.run([str(named), '-c', launcher, str(driver), '/', 'fixture-old-version'],
                               capture_output=True, text=True, timeout=10)
            self.assertEqual(r.returncode, 0, r.stdout+r.stderr)
            self.assertIn('graphical session still running', r.stdout)
            self.assertIsNone(client.poll(), 'ancestor compositor exemption allowed a client signal')

    def test_required_step_retry_and_service_restart(self):
        self.install_a()
        helper=self.root/'usr/share/tamlinux/rootstock/action'
        helper.write_text('#!/bin/sh\necho "$1" >> "'+str(self.base/'actions')+'"\n[ "$1" != apply ] || [ -f "'+str(self.base/'allow')+'" ]\n')
        helper.chmod(0o755)
        profile=helper.parent/'profile'
        profile.write_text(profile.read_text()+'services usr/share/tamlinux/rootstock/action\nstep test required usr/share/tamlinux/rootstock/action\n')
        self.tb('install',self.b);self.tb('apply',code=3)
        self.assertEqual(self.tx()['state'],'activating')
        self.tb('cancel',code=4)
        (self.base/'allow').touch();self.tb('apply');self.assert_live(self.b)
        self.assertIn('restart-new',(self.base/'actions').read_text())

    def test_every_durable_transition(self):
        self.install_a()
        baseline=self.base/'baseline';shutil.copytree(self.root,baseline,symlinks=True)
        trace=self.base/'prepare.trace'
        self.tb('install',self.b,env={'TAM_BUNDLE_TRACE':str(trace)})
        prepare_count=len(trace.read_text().splitlines())
        trace2=self.base/'apply.trace'
        self.tb('apply',env={'TAM_BUNDLE_TRACE':str(trace2)})
        apply_count=len(trace2.read_text().splitlines())
        for phase,count in [('prepare',prepare_count),('apply',apply_count)]:
            for point in range(1,count+1):
                with self.subTest(phase=phase,point=point):
                    shutil.rmtree(self.root);shutil.copytree(baseline,self.root,symlinks=True)
                    if phase=='apply':self.tb('install',self.b)
                    self.tb('install' if phase=='prepare' else 'apply', *([self.b] if phase=='prepare' else []),
                            code=99,env={'TAM_BUNDLE_FAULT_AT':str(point)})
                    tx=self.tx()
                    if phase=='prepare':
                        self.assertIn('echo A',(self.root/'usr/bin/tam-demo').read_text())
                        if tx and tx['state']!='complete':self.tb('cancel')
                        self.tb('install',self.b)
                    self.tb('apply')
                    self.assert_live(self.b)
        print(f'  checked {prepare_count} prepare and {apply_count} apply durable transitions')

    def test_random_sigkill(self):
        self.install_a();baseline=self.base/'baseline';shutil.copytree(self.root,baseline,symlinks=True)
        rng=random.Random(600)
        for i in range(12):
            shutil.rmtree(self.root);shutil.copytree(baseline,self.root,symlinks=True)
            self.tb('install',self.b)
            p=subprocess.Popen([str(BIN),'--root',str(self.root),'apply'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,
                               env=dict(os.environ,TAM_BUNDLE_SLOW_US='5000'))
            time.sleep(rng.uniform(.001,.06))
            if p.poll() is None:p.kill()
            p.wait();self.tb('apply');self.assert_live(self.b)

if __name__=='__main__':
    enter_namespace(__file__)
    unittest.main(verbosity=2)
