#!/usr/bin/env python3
"""Private settings fixtures; no live home or host process is reachable."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tarfile
import random
import time
import unittest
import format2
from pid_namespace import enter_namespace, require_namespace


def setUpModule():
    require_namespace()


class UserSettings(unittest.TestCase):
    bundle = format2.ProductTransactions.bundle
    id = format2.ProductTransactions.id
    tb = format2.ProductTransactions.tb
    install_a = format2.ProductTransactions.install_a
    tx = format2.ProductTransactions.tx
    assert_live = format2.ProductTransactions.assert_live

    def setUp(self):
        format2.ProductTransactions.setUp(self)
        self.home=self.base/'home'; self.home.mkdir()
        profile=self.root/'usr/share/tamlinux/rootstock/profile'
        helper=profile.parent/'reload'; helper.write_text('#!/bin/sh\nexit 0\n'); helper.chmod(0o755)
        profile.write_text(profile.read_text()+'user-step user-units-reload required usr/share/tamlinux/rootstock/reload\n')
        self.config={'common': {'.bashrc':'old bash\n', '.config/edited':'old edited\n',
                                '.config/dropped':'old dropped\n'},
                     'host': {'.config/monitor':'old monitor\n'}}
        self.configure(self.a,self.config)
        second={'common': {'.bashrc':'new bash\n', '.config/edited':'new edited\n',
                           '.config/added':'new added\n'},
                'host': {'.config/monitor':'new monitor\n'}}
        self.configure(self.b,second)
        self.install_a()

    def tearDown(self):
        format2.ProductTransactions.tearDown(self)

    def run_tb(self,*args,env=None,**kw):
        values=dict(env or {})
        if hasattr(self,'home'):values['HOME']=str(self.home)
        return format2.ProductTransactions.run_tb(self,*args,env=values,**kw)

    def configure(self,bundle,groups):
        text='format 1\n'
        for mark,files in groups.items():
            for path,data in files.items():
                text+=f'{mark} {path}\n'
                p=bundle/'tree/defaults/home'/path; p.parent.mkdir(parents=True,exist_ok=True); p.write_text(data)
        (bundle/'SETTINGS').write_text(text)
        (bundle/'BUNDLE').unlink(); (bundle/'MANIFEST').unlink()
        self.run_tb('seal',bundle,'--format','2','--name','demo','--version',bundle.name)

    def write(self,path,data):
        p=self.home/path; p.parent.mkdir(parents=True,exist_ok=True); p.write_text(data)

    @property
    def state(self):
        return self.home/'.local/state/tamlinux'

    def capture(self):
        before=set((self.state/'captures').iterdir()) if (self.state/'captures').exists() else set()
        result=self.tb('capture')
        new=set((self.state/'captures').iterdir())-before
        self.assertEqual(len(new),1)
        return new.pop(),result

    def test_private_capture_presence_secrets_and_identity(self):
        self.write('.bashrc','mine\n'); self.write('.config/edited','password=planted-test-secret\n')
        self.write('.ssh/id_ed25519','private key')
        self.state.mkdir(parents=True,exist_ok=True)
        (self.state/'settings.list').write_text('format 1\ncommon .ssh/id_ed25519\n')
        capture,result=self.capture()
        self.assertNotIn('planted-test-secret',result.stdout+result.stderr)
        records={f[-1]:f for line in (capture/'FILES').read_text().splitlines()[1:] if (f:=shlex.split(line))}
        self.assertEqual(records['.bashrc'][1],'present')
        self.assertEqual(records['.config/dropped'][1],'absent')
        self.assertEqual(records['.config/edited'][1],'excluded')
        self.assertEqual(records['.ssh/id_ed25519'][1],'excluded')
        self.assertIn('complete no',(capture/'CAPTURE').read_text())
        for p in capture.rglob('*'):
            self.assertEqual(p.stat().st_mode&0o777,0o700 if p.is_dir() else 0o600)
        other,_=self.capture(); self.assertNotEqual(capture.name,other.name)
        self.assertFalse((capture/'files/.config/edited').exists())

    def test_settings_restore_presence_and_host_marks(self):
        self.write('.bashrc','captured'); self.write('.config/monitor','captured monitor')
        self.write('.config/edited','token=private-test-value')
        capture,_=self.capture()
        self.write('.bashrc','later'); self.write('.config/dropped','remove me')
        self.write('.config/edited','do not change excluded')
        self.tb('restore-settings',capture.name)
        self.tb('session-ready',code=4)
        self.tb('apply-settings')
        self.assertEqual((self.home/'.bashrc').read_text(),'captured')
        self.assertFalse((self.home/'.config/dropped').exists())
        self.assertEqual((self.home/'.config/edited').read_text(),'do not change excluded')
        self.tb('session-ready')
        self.write('.bashrc','other'); self.write('.config/monitor','target monitor')
        (self.root/'var/lib/tamlinux/host-id').write_text('sha256:'+'b'*64+'\n')
        self.tb('restore-settings',capture.name); self.tb('apply-settings')
        self.assertEqual((self.home/'.bashrc').read_text(),'captured')
        self.assertEqual((self.home/'.config/monitor').read_text(),'target monitor')

    def test_upgrade_computes_at_switch_and_captures_current_state(self):
        for files in self.config.values():
            for path,data in files.items():self.write(path,data)
        self.tb('install',self.b); self.tb('prepare-settings')
        self.assertFalse((self.state/'captures').exists())
        self.write('.config/edited','changed after prepare\n')
        self.tb('session-ready',code=4)
        self.tb('apply'); self.tb('session-ready',code=4); self.tb('apply-settings')
        self.assertEqual((self.home/'.bashrc').read_text(),'new bash\n')
        self.assertEqual((self.home/'.config/edited').read_text(),'changed after prepare\n')
        self.assertEqual((self.home/'.config/edited.tamlinux-new').read_text(),'new edited\n')
        self.assertFalse((self.home/'.config/dropped').exists())
        self.assertEqual((self.home/'.config/added').read_text(),'new added\n')
        captures=list((self.state/'captures').iterdir()); self.assertEqual(len(captures),1)
        self.assertEqual((captures[0]/'files/.config/edited').read_text(),'changed after prepare\n')
        self.tb('session-ready')

    def test_corrupt_capture_and_symlink_are_not_restored(self):
        self.write('.bashrc','original'); capture,_=self.capture()
        (capture/'files/.bashrc').write_text('corrupt')
        self.tb('restore-settings',capture.name,code=1)
        target=self.base/'outside'; target.write_text('outside untouched')
        (self.home/'.bashrc').unlink(); (self.home/'.bashrc').symlink_to(target)
        captured,_=self.capture()
        self.assertIn('excluded',(captured/'FILES').read_text())
        self.assertEqual(target.read_text(),'outside untouched')

    def test_workstation_export_import_into_fresh_home(self):
        self.write('.bashrc','portable settings\n')
        archive=self.base/'workstation.tbundle'
        self.tb('capture','-o',archive)
        self.assertEqual(archive.stat().st_mode&0o777,0o600)
        self.run_tb('verify',archive)
        with tarfile.open(archive) as t:
            self.assertIn('capture/CAPTURE',t.getnames())
            self.assertNotIn('private-test',t.getnames())
        original=self.home; self.home=self.base/'fresh-home'; self.home.mkdir()
        imported=self.tb('import-settings',archive)
        capture=next((self.state/'captures').iterdir())
        self.assertIn(capture.name,imported.stdout)
        shutil.rmtree(original)
        self.tb('restore-settings',capture.name); self.tb('apply-settings')
        self.assertEqual((self.home/'.bashrc').read_text(),'portable settings\n')
        self.tb('session-ready')
        self.assertFalse(list((self.root/'var/lib/tamlinux').rglob('CAPTURE')))

    def test_workstation_installs_only_product_into_fresh_root(self):
        self.write('.bashrc','from workstation\n')
        archive=self.base/'portable.tbundle'; self.tb('capture','-o',archive)
        source=self.root; fresh=self.base/'second-root'
        shutil.copytree(source/'usr/share/tamlinux/rootstock',fresh/'usr/share/tamlinux/rootstock')
        self.root=fresh
        self.tb('install',archive); self.tb('apply'); self.tb('check')
        self.home=self.base/'second-home'; self.home.mkdir()
        self.tb('import-settings',archive)
        capture=next((self.state/'captures').iterdir())
        self.tb('restore-settings',capture.name); self.tb('apply-settings'); self.tb('session-ready')
        self.assertEqual((self.home/'.bashrc').read_text(),'from workstation\n')
        self.assertFalse(list((fresh/'var/lib/tamlinux').rglob('CAPTURE')))
        self.assertFalse(list((fresh/'usr/lib/tamlinux').rglob('CAPTURE')))

    def test_foreign_edit_blocks_all_settings_recovery(self):
        self.write('.bashrc','captured'); capture,_=self.capture(); self.write('.bashrc','before')
        self.tb('restore-settings',capture.name)
        self.tb('apply-settings',code=99,env={'TAM_BUNDLE_FAULT':'settings-state-applying'})
        self.write('.bashrc','external edit')
        self.tb('apply-settings',code=4)
        self.assertEqual((self.home/'.bashrc').read_text(),'external edit')
        self.write('.bashrc','before'); self.tb('apply-settings'); self.tb('session-ready')

    def test_settings_diff_and_consumed_new_default_cleanup(self):
        self.write('.config/edited','custom\n')
        self.tb('install',self.b); self.tb('prepare-settings')
        result=self.tb('settings','diff','.config/edited')
        self.assertIn('custom',result.stdout); self.assertIn('old edited',result.stdout); self.assertIn('new edited',result.stdout)
        self.tb('apply'); self.tb('apply-settings')
        self.write('.config/edited','new edited\n')
        c=self.bundle('C'); self.configure(c,{'common': {'.config/edited':'new edited\n'}})
        self.tb('install',c); self.tb('prepare-settings'); self.tb('apply'); self.tb('apply-settings')
        self.assertFalse((self.home/'.config/edited.tamlinux-new').exists())
        self.write('.config/edited','password=never-display-this')
        r=self.tb('settings','diff','.config/edited',code=1)
        self.assertNotIn('never-display-this',r.stdout+r.stderr)

    def test_stale_settings_transaction_does_not_pass_after_product_round_trip(self):
        self.write('.bashrc','captured'); capture,_=self.capture()
        self.tb('restore-settings',capture.name); self.tb('apply-settings'); self.tb('session-ready')
        self.tb('install',self.b); self.tb('apply')
        self.tb('restore',self.a); self.tb('apply')
        self.tb('session-ready',code=4)

    def test_random_kills_restore_settings_forward(self):
        self.write('.bashrc','captured'); capture,_=self.capture()
        self.write('.bashrc','before'); self.tb('restore-settings',capture.name)
        baseline=self.base/'kill-baseline'; shutil.copytree(self.home,baseline,symlinks=True)
        rng=random.Random(707)
        for _ in range(12):
            shutil.rmtree(self.home); shutil.copytree(baseline,self.home,symlinks=True)
            p=subprocess.Popen([str(format2.BIN),'--root',str(self.root),'apply-settings'],
                env=dict(os.environ,HOME=str(self.home),TAM_BUNDLE_SLOW_US='10000'),
                stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            time.sleep(rng.uniform(.001,.025))
            if p.poll() is None:p.kill()
            p.wait(); self.tb('apply-settings'); self.tb('session-ready')
            self.assertEqual((self.home/'.bashrc').read_text(),'captured')

    def test_export_edited_product_requires_derived_identity(self):
        (self.root/'usr/bin/tam-demo').write_text('edited executable\n')
        archive=self.base/'derived.tbundle'
        self.tb('capture','-o',archive,code=1)
        self.assertFalse(archive.exists())
        self.tb('capture','-o',archive,'--save-edits')
        self.run_tb('verify',archive)
        with tarfile.open(archive) as t:
            bundle=t.extractfile('BUNDLE').read().decode()
            captured=t.extractfile('capture/CAPTURE').read().decode()
            self.assertIn('derived-from '+self.id(self.a),bundle)
            self.assertIn('edit placed/usr/bin/tam-demo',bundle)
            self.assertEqual(t.extractfile('placed/usr/bin/tam-demo').read(),b'edited executable\n')
            fields=dict(line.split(' ',1) for line in bundle.splitlines() if line.startswith(('name ','version ','hash ')))
            derived=f"{fields['name']}-{fields['version']}-{fields['hash'][7:19]}"
            self.assertIn('product '+derived,captured)
        public=self.root/'var/lib/tamlinux/private.tbundle'
        self.tb('capture','-o',public,'--save-edits',code=1)
        self.assertFalse(public.exists())

    def test_user_activation_failure_and_recovery_edit_block(self):
        self.write('.bashrc','captured'); capture,_=self.capture(); self.write('.bashrc','later')
        helper=self.root/'usr/share/tamlinux/rootstock/reload'
        helper.write_text('#!/bin/sh\nexit 1\n')
        self.tb('restore-settings',capture.name); self.tb('apply-settings',code=3)
        self.tb('session-ready',code=4)
        self.write('.bashrc','external writer')
        helper.write_text('#!/bin/sh\nexit 0\n')
        self.tb('apply-settings',code=4)
        self.write('.bashrc','captured'); self.tb('apply-settings'); self.tb('session-ready')

    def test_every_user_apply_transition_recovers(self):
        self.write('.bashrc','captured'); capture,_=self.capture()
        self.write('.bashrc','restore from this'); self.write('.config/dropped','remove this')
        self.tb('restore-settings',capture.name)
        baseline=self.base/'home-baseline'; shutil.copytree(self.home,baseline,symlinks=True)
        trace=self.base/'settings.trace'
        self.tb('apply-settings',env={'TAM_BUNDLE_TRACE':str(trace)})
        count=len(trace.read_text().splitlines())
        for point in range(1,count+1):
            with self.subTest(point=point):
                shutil.rmtree(self.home); shutil.copytree(baseline,self.home,symlinks=True)
                self.tb('apply-settings',code=99,env={'TAM_BUNDLE_FAULT_AT':str(point)})
                self.tb('apply-settings')
                self.assertEqual((self.home/'.bashrc').read_text(),'captured')
                self.assertFalse((self.home/'.config/dropped').exists())
                self.tb('session-ready')
        print(f'  checked {count} user apply durable transitions')


if __name__=='__main__':
    enter_namespace(__file__)
    unittest.main(verbosity=2)
