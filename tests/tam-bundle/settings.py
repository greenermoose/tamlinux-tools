#!/usr/bin/env python3
"""Private settings fixtures; no live home or host process is reachable."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tarfile
import random
import hashlib
import io
import signal
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
        shutil.copytree(source/'var/lib/pacman',fresh/'var/lib/pacman')
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

    def test_ready_switch_keeps_the_complete_old_session_available(self):
        self.write('.bashrc','old session')
        capture,_=self.capture()
        self.tb('restore-settings',capture.name)
        self.tb('apply-settings')
        self.tb('install',self.b)
        self.tb('session-ready')
        self.tb('prepare-settings')
        previous=self.state/'last-transaction'
        self.assertEqual(previous.stat().st_mode&0o777,0o600)
        self.tb('session-ready')
        original=previous.read_text()
        previous.write_text(original.replace('to '+self.id(self.a),'to foreign'))
        self.tb('session-ready',code=4)
        previous.write_text(original)
        self.tb('apply',code=99,env={'TAM_BUNDLE_FAULT':'v2-state-applying'})
        self.tb('session-ready',code=4)
        self.tb('apply')
        self.tb('session-ready',code=4)
        self.tb('apply-settings')
        self.tb('session-ready')

    def test_console_trigger_completes_system_and_user_or_recovers(self):
        helper=self.root/'usr/share/tamlinux/rootstock/reload'
        actions=self.base/'unit-actions'
        helper.write_text('#!/bin/sh\necho "$1" >> "'+str(actions)+'"\n')
        helper.chmod(0o755)
        pkexec=self.base/'pkexec'
        pkexec.write_text('#!/bin/sh\nexec "'+str(format2.BIN)+'" --root "'+str(self.root)+'" apply "$2"\n')
        pkexec.chmod(0o755)
        env={'TAM_BUNDLE_PKEXEC':str(pkexec)}
        self.tb('session-activate','unexpected',code=2,env=env)
        self.write('.bashrc','console edit')
        self.tb('install',self.b)
        self.tb('apply','0'*32,code=4)
        self.assertEqual(self.tx()['state'],'ready')
        self.tb('prepare-settings')
        self.tb('session-activate',env=env)
        self.tb('session-ready')
        self.assertEqual(actions.read_text().splitlines(),['stop','apply'])
        self.assertEqual((self.home/'.bashrc').read_text(),'console edit')
        self.assert_live(self.b)
        (self.home/'.bashrc.tamlinux-new').unlink()  # The user has resolved the offered default.
        self.tb('install',self.a)
        self.tb('prepare-settings')
        pkexec.write_text('#!/bin/sh\nexit 126\n')
        self.tb('session-activate',env=env)
        self.assertEqual(self.tx()['state'],'ready')
        self.assertEqual(actions.read_text().splitlines()[-2:],['stop','restart-old'])
        self.tb('session-ready')
        pkexec.write_text('#!/bin/sh\nexec "'+str(format2.BIN)+'" --root "'+str(self.root)+'" apply "$2"\n')
        self.tb('session-activate',env=dict(env,TAM_BUNDLE_FAULT='v2-state-applying'),code=4)
        self.tb('session-ready',code=4)
        self.tb('session-activate',env=env)
        self.tb('session-ready')
        self.assert_live(self.a)

    def test_console_trigger_decline_preserves_the_completed_old_installation(self):
        capture,_=self.capture()
        self.tb('restore-settings',capture.name)
        self.tb('apply-settings')
        self.tb('install',self.b)
        self.tb('prepare-settings')
        refused=self.base/'must-not-run'
        pkexec=self.base/'pkexec'
        pkexec.write_text('#!/bin/sh\ntouch "'+str(refused)+'"\nexit 1\n')
        pkexec.chmod(0o755)
        with format2.ProductTransactions.sleeper(self,argument=self.root/'usr/lib/tamlinux/current/share/value') as blocker:
            run=subprocess.run([str(format2.BIN),'--root',str(self.root),'session-activate'],
                               input='n\n',text=True,capture_output=True,timeout=15,
                               env=dict(os.environ,HOME=str(self.home),TAM_BUNDLE_PKEXEC=str(pkexec)))
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            self.assertIsNone(blocker.poll())
            self.assertIn('[y/N]',run.stdout)
        self.assertFalse(refused.exists())
        self.assertEqual(self.tx()['state'],'ready')
        self.tb('session-ready')

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

    def reset_home(self,baseline):
        shutil.rmtree(self.home); shutil.copytree(baseline,self.home,symlinks=True)

    def enumerate_faults(self,label,command,baseline,check):
        trace=self.base/(label+'.trace')
        self.tb(*command,env={'TAM_BUNDLE_TRACE':str(trace)})
        count=len(trace.read_text().splitlines())
        self.assertGreater(count,0)
        for point in range(1,count+1):
            with self.subTest(operation=label,point=point):
                self.reset_home(baseline)
                check(point)
        print(f'  checked {count} {label} fault events')

    def test_capture_and_prepare_faults(self):
        self.write('.bashrc','keep this setting')
        baseline=self.base/'capture-baseline'; shutil.copytree(self.home,baseline)
        def capture_fault(point):
            self.tb('capture',code=99,env={'TAM_BUNDLE_FAULT_AT':str(point)})
            self.tb('capture')
            for cap in (self.state/'captures').iterdir():
                self.assertEqual((cap/'files/.bashrc').read_text(),'keep this setting')
                self.tb('restore-settings',cap.name)
            self.assertEqual((self.home/'.bashrc').read_text(),'keep this setting')
        self.enumerate_faults('capture',('capture',),baseline,capture_fault)
        self.reset_home(baseline)
        capture,_=self.capture()
        self.tb('install',self.b)
        planbase=self.base/'plan-baseline'; shutil.copytree(self.home,planbase)
        def plan_fault(point):
            self.tb('prepare-settings',code=99,env={'TAM_BUNDLE_FAULT_AT':str(point)})
            self.tb('prepare-settings')
            self.assertEqual((self.home/'.bashrc').read_text(),'keep this setting')
            self.tb('session-ready',code=4)
        self.enumerate_faults('prepare-settings',('prepare-settings',),planbase,plan_fault)
        self.tb('cancel'); self.reset_home(planbase)
        def restore_plan_fault(point):
            self.tb('restore-settings',capture.name,code=99,env={'TAM_BUNDLE_FAULT_AT':str(point)})
            self.tb('restore-settings',capture.name)
            self.assertEqual((self.home/'.bashrc').read_text(),'keep this setting')
            self.tb('session-ready',code=4)
        self.enumerate_faults('restore-settings',('restore-settings',capture.name),planbase,restore_plan_fault)

    def test_export_and_import_faults(self):
        self.write('.bashrc','portable fault proof')
        baseline=self.base/'export-baseline'; shutil.copytree(self.home,baseline)
        output=self.base/'fault-export.tbundle'
        def export_fault(point):
            output.unlink(missing_ok=True)
            self.tb('capture','-o',output,code=99,env={'TAM_BUNDLE_FAULT_AT':str(point)})
            if not output.exists():self.tb('capture','-o',output)
            self.assertEqual(output.stat().st_mode&0o777,0o600)
            self.run_tb('verify',output)
            before=output.read_bytes()
            self.tb('capture','-o',output,code=1)
            self.assertEqual(output.read_bytes(),before)
        self.enumerate_faults('export',('capture','-o',output),baseline,export_fault)
        self.reset_home(baseline)
        importbase=self.base/'import-baseline'; shutil.copytree(self.home,importbase)
        def import_fault(point):
            self.tb('import-settings',output,code=99,env={'TAM_BUNDLE_FAULT_AT':str(point)})
            self.tb('import-settings',output); self.tb('import-settings',output)
            caps=list((self.state/'captures').iterdir()); self.assertEqual(len(caps),1)
            self.write('.bashrc','later')
            self.tb('restore-settings',caps[0].name); self.tb('apply-settings'); self.tb('session-ready')
            self.assertEqual((self.home/'.bashrc').read_text(),'portable fault proof')
        self.enumerate_faults('import',('import-settings',output),importbase,import_fault)

    def test_activation_profile_change_restarts_required_steps(self):
        self.write('.bashrc','capture'); capture,_=self.capture()
        profile=self.root/'usr/share/tamlinux/rootstock/profile'
        first=profile.parent/'reload'; second=profile.parent/'second'
        second.write_text('#!/bin/sh\nexit 1\n'); second.chmod(0o755)
        profile.write_text(profile.read_text()+'user-step second required usr/share/tamlinux/rootstock/second\n')
        self.tb('restore-settings',capture.name); self.tb('apply-settings',code=3)
        tx=self.state/'transaction'; self.assertIn('done 1',tx.read_text())
        original_profile=profile.read_text(); profile.unlink(); os.mkfifo(profile)
        self.tb('apply-settings',code=3); self.tb('session-ready',code=4)
        profile.unlink(); profile.write_text(original_profile)
        # A replacement first step must run even though old step 1 succeeded.
        replacement=profile.parent/'replacement'
        replacement.write_text('#!/bin/sh\nexit 1\n'); replacement.chmod(0o755)
        profile.write_text(profile.read_text().replace('rootstock/reload','rootstock/replacement'))
        second.write_text('#!/bin/sh\nexit 0\n')
        self.tb('apply-settings',code=3)
        self.assertIn('done 0',tx.read_text()); self.tb('session-ready',code=4)
        replacement.write_text('#!/bin/sh\nexit 0\n'); self.tb('apply-settings'); self.tb('session-ready')

    def test_retry_metadata_is_bound_and_strict(self):
        self.write('.bashrc','capture'); capture,_=self.capture(); self.write('.bashrc','later')
        self.tb('restore-settings',capture.name)
        self.tb('apply-settings',code=99,env={'TAM_BUNDLE_FAULT':'settings-state-applying'})
        tx=self.state/'transaction'; original=tx.read_text()
        stage=next((self.state/'staging').iterdir()); changes=stage/'CHANGES'; original_changes=changes.read_text()
        mutations=[original+'done 0\n',original.replace('done 0','done -1'),
                   original.replace('done 0','done 129'),original.replace('state applying','state ready'),
                   original.replace('format 1','format 2'), original.replace('automatic ','unknown ')]
        for text in mutations:
            with self.subTest(record=text):
                tx.write_text(text); self.tb('apply-settings',code=4)
                self.assertEqual((self.home/'.bashrc').read_text(),'later')
        tx.write_text(original)
        for text in ['format 1\n',original_changes+'garbage\n']:
            changes.write_text(text); self.tb('apply-settings',code=4)
            self.assertEqual((self.home/'.bashrc').read_text(),'later')
        changes.write_text(original_changes)
        self.tb('apply-settings'); self.tb('session-ready')

    def test_random_kills_capture_prepare_export_import(self):
        self.write('.bashrc','portable')
        archive=self.base/'kill-source.tbundle'; self.tb('capture','-o',archive)
        baseline=self.base/'kill-operations'; shutil.copytree(self.home,baseline)
        rng=random.Random(708)
        for command in [('capture',),('restore-settings',next((self.state/'captures').iterdir()).name),
                        ('capture','-o',self.base/'kill-export.tbundle'),('import-settings',archive)]:
            for _ in range(6):
                self.reset_home(baseline)
                output=self.base/'kill-export.tbundle'; output.unlink(missing_ok=True)
                p=subprocess.Popen([str(format2.BIN),'--root',str(self.root),*map(str,command)],
                    env=dict(os.environ,HOME=str(self.home),TAM_BUNDLE_SLOW_US='5000'),
                    stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
                time.sleep(rng.uniform(.001,.02))
                if p.poll() is None:p.kill()
                p.wait()
                if command[0]=='capture' and len(command)>1 and output.exists():self.run_tb('verify',output)
                else:self.tb(*command)
                self.assertEqual((self.home/'.bashrc').read_text(),'portable')

    def test_system_record_and_exact_transaction_agreement(self):
        self.write('.bashrc','capture'); capture,_=self.capture()
        self.tb('restore-settings',capture.name); self.tb('apply-settings')
        sys=self.root/'var/lib/tamlinux/transaction'; original=sys.read_text()
        mutations=[original+'uid 0\n',original.replace('kind product-only','kind unknown'),
                   original.replace('capture -','capture foreign'),original.replace('uid 0','uid 1'),
                   original.replace('from -','from ../wrong'), original.replace('to '+self.id(self.a),'to wrong'),
                   original.replace('done 0','done -1')]
        for text in mutations:
            with self.subTest(system=text):
                sys.write_text(text); self.tb('session-ready',code=4); self.tb('apply-settings',code=4)
        sys.write_text(original)
        self.tb('session-ready')
        self.tb('install',self.b); self.tb('prepare-settings'); self.tb('apply')
        user=self.state/'transaction'; original=user.read_text()
        user.write_text(original.replace('from '+self.id(self.a),'from foreign'))
        self.tb('apply-settings',code=4)
        user.write_text(original); self.tb('apply-settings'); self.tb('session-ready')

    def test_metadata_limits_fifo_and_stable_read_exclusion(self):
        self.state.mkdir(parents=True,exist_ok=True)
        listing=self.state/'settings.list'
        listing.write_text('format 1\ncommon .config/a file\n')
        self.write('.config/a file','spaces survive')
        cap,_=self.capture(); self.tb('restore-settings',cap.name)
        with listing.open('w') as f:f.truncate(16*1024*1024+1)
        self.tb('capture',code=1)
        listing.unlink(); os.mkfifo(listing); self.tb('capture',code=1); listing.unlink()
        self.write('.bashrc','initial')
        p=subprocess.Popen([str(format2.BIN),'--root',str(self.root),'capture'],
            env=dict(os.environ,HOME=str(self.home),TAM_BUNDLE_READ_BARRIER='.bashrc'),
            stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        try:
            for i in range(3):
                # The test seam stops exactly between reading and rechecking.
                deadline=time.monotonic()+5
                while time.monotonic()<deadline:
                    pid,status=os.waitpid(p.pid,os.WNOHANG|os.WUNTRACED)
                    if pid:break
                    time.sleep(.001)
                self.assertTrue(pid and os.WIFSTOPPED(status))
                temp=self.home/'.swap'; temp.write_text(f'changed {i}'); temp.replace(self.home/'.bashrc')
                p.send_signal(signal.SIGCONT)
            out,err=p.communicate(timeout=5); self.assertEqual(p.returncode,0,err)
            self.assertIn('changed-during-read',out)
            self.assertNotIn('changed 2',out+err)
        finally:
            if p.poll() is None:p.kill(); p.wait()

    def rewrite_archive(self,source,dest,change):
        with tarfile.open(source) as t:
            entries=[(e,t.extractfile(e).read() if e.isfile() else None) for e in t]
        change(entries)
        with tarfile.open(dest,'w',format=tarfile.PAX_FORMAT) as t:
            for e,data in entries:
                if data is not None:e.size=len(data)
                t.addfile(e,io.BytesIO(data) if data is not None else None)

    def test_hostile_capture_archives_and_semantic_metadata(self):
        self.write('.bashrc','safe'); source=self.base/'source.tbundle'; self.tb('capture','-o',source)
        def metadata_change(header=None,files=None):
            def change(entries):
                data={e.name:b for e,b in entries}
                h=data['capture/CAPTURE'].decode(); f=data['capture/FILES'].decode()
                h=''.join(line+'\n' for line in h.splitlines() if not line.startswith('hash '))
                if header:h=header(h)
                if files:f=files(f)
                h+='hash sha256:'+hashlib.sha256((h+f).encode()).hexdigest()+'\n'
                for i,(e,b) in enumerate(entries):
                    if e.name=='capture/CAPTURE':entries[i]=(e,h.encode())
                    if e.name=='capture/FILES':entries[i]=(e,f.encode())
            return change
        cases=[metadata_change(header=lambda h:h+'user 0\n'),
               metadata_change(header=lambda h:h.replace('user 0','user '+'9'*300)),
               metadata_change(header=lambda h:h.replace('created ','created 999')),
               metadata_change(header=lambda h:h.replace('complete yes','complete no')),
               metadata_change(files=lambda f:f.replace(' edited ',' - ')),
               metadata_change(files=lambda f:f.replace(' 0 .bashrc',' 999999999999999999999999 .bashrc')),
               metadata_change(files=lambda f:f.replace(self.id(self.a),'foreign')),
               metadata_change(files=lambda f:f.replace(' .bashrc',' ../../escape'))]
        def extra(entries):
            e=tarfile.TarInfo('capture/unlisted'); e.mode=0o600; entries.append((e,b'foreign'))
        def link(entries):
            for i,(e,b) in enumerate(entries):
                if e.name=='capture/files/.bashrc':
                    e.type=tarfile.SYMTYPE; e.linkname='/etc/passwd'; entries[i]=(e,None)
        cases.extend([extra,link])
        for i,change in enumerate(cases):
            with self.subTest(case=i):
                archive=self.base/f'hostile-{i}.tbundle'; self.rewrite_archive(source,archive,change)
                self.run_tb('verify',archive,code=1); self.tb('import-settings',archive,code=1)
                self.assertEqual((self.home/'.bashrc').read_text(),'safe')


if __name__=='__main__':
    enter_namespace(__file__)
    unittest.main(verbosity=2)
