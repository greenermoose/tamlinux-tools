#!/usr/bin/env python3
"""Native databases, runtime contracts and ELF probes in isolated fixtures."""
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import unittest
import format2
from pid_namespace import enter_namespace, require_namespace


def setUpModule():
    require_namespace()


class Runtime(unittest.TestCase):
    bundle=format2.ProductTransactions.bundle
    id=format2.ProductTransactions.id
    tb=format2.ProductTransactions.tb
    tx=format2.ProductTransactions.tx
    run_tb=format2.ProductTransactions.run_tb
    install_a=format2.ProductTransactions.install_a
    assert_live=format2.ProductTransactions.assert_live
    setUp=format2.ProductTransactions.setUp
    tearDown=format2.ProductTransactions.tearDown

    def package(self,name,version,files):
        for p in (self.root/'var/lib/pacman/local').glob(name+'-*'):shutil.rmtree(p)
        p=self.root/f'var/lib/pacman/local/{name}-{version}'; p.mkdir()
        (p/'desc').write_text(f'%NAME%\n{name}\n\n%VERSION%\n{version}\n\n%DESC%\nfixture\n\n%ARCH%\nx86_64\n\n')
        (p/'files').write_text('%FILES%\n'+'\n'.join(files)+'\n\n')

    def reseal(self,bundle,needs=(),targets=()):
        for name in ['BUNDLE','MANIFEST']:(bundle/name).unlink(missing_ok=True)
        args=[]
        for need in needs:args.extend(['--needs',need])
        for target in targets:args.extend(['--target',target])
        self.run_tb('seal',bundle,'--format','2','--name','demo','--version',bundle.name,*args)

    def root_file(self,path,data=b'dependency',mode=0o644):
        p=self.root/path; p.parent.mkdir(parents=True,exist_ok=True); p.write_bytes(data); p.chmod(mode); return p

    def test_native_ownership_includes_missing_files_and_retry(self):
        self.package('foreign','1-1',['usr/bin/tam-demo'])
        self.tb('install',self.a,code=1)
        self.assertFalse((self.root/'usr/bin/tam-demo').exists())
        shutil.rmtree(next((self.root/'var/lib/pacman/local').glob('foreign-*')))
        self.install_a(); self.tb('install',self.b)
        self.package('foreign','2-1',['usr/bin/tam-demo'])
        self.tb('apply',code=4)
        self.assertEqual((self.root/'usr/bin/tam-demo').read_bytes(),(self.a/'placed/usr/bin/tam-demo').read_bytes())
        shutil.rmtree(next((self.root/'var/lib/pacman/local').glob('foreign-*')))
        self.tb('apply'); self.assert_live(self.b)

    def test_native_database_failure_and_unrelated_software(self):
        foreign=self.root_file('usr/local/bin/other',b'untouched',0o755)
        self.package('extra','1.0-1',['usr/local/bin/other'])
        lock=self.root/'var/lib/pacman/db.lck'; lock.write_text('package operation')
        self.tb('install',self.a,code=1); lock.unlink()
        self.install_a(); self.tb('install',self.b); self.tb('apply')
        self.assertEqual(foreign.read_bytes(),b'untouched')
        shutil.rmtree(self.root/'var/lib/pacman/local')
        self.tb('needs',self.b,code=1)

    def test_post_transaction_check_reads_locked_database(self):
        dep=self.root_file('usr/lib/native-dependency')
        self.package('library','2.0-1',['usr/lib/native-dependency'])
        self.reseal(self.a,needs=['file /usr/lib/native-dependency >= 2.0 arch=library'])
        self.install_a()
        (self.root/'var/lib/pacman/db.lck').write_text('package operation')
        self.assertIn('compatible',self.tb('native-status').stdout)
        self.tb('install',self.b,code=1)
        dep.unlink()
        result=self.tb('native-status',code=1)
        self.assertIn('unmet runtime need: file /usr/lib/native-dependency',result.stderr)

    def test_targets_are_checked_before_needs(self):
        self.reseal(self.a,needs=['file /missing'],targets=['arch aarch64 baseline x86-64-v2'])
        r=self.tb('needs',self.a,code=1)
        self.assertIn('incompatible',r.stderr); self.assertNotIn('unmet runtime need',r.stderr)
        self.reseal(self.a,targets=['profile arch-systemd'])
        self.tb('needs',self.a)
        (self.root/'usr/share/tamlinux/rootstock/id').write_text('other\n')
        self.tb('needs',self.a,code=1)
        self.reseal(self.a,targets=['arch x86_64 baseline imaginary'])
        self.tb('needs',self.a,code=1)

    def test_minimum_exact_unknown_and_tool_versions(self):
        dep=self.root_file('usr/lib/libfixture.so.1')
        self.package('library','2.0-1',['usr/lib/libfixture.so.1'])
        self.reseal(self.a,needs=['file /usr/lib/libfixture.so.1 >= 2.0 arch=library', 'tam-bundle >= 0.8.0'])
        self.tb('needs',self.a)
        self.package('library','1.9-1',['usr/lib/libfixture.so.1'])
        self.tb('needs',self.a,code=1)
        self.package('library','2.1-1',['usr/lib/libfixture.so.1']); self.tb('needs',self.a)
        digest=hashlib.sha256(dep.read_bytes()).hexdigest()
        self.reseal(self.a,needs=['file /usr/lib/libfixture.so.1 exact sha256:'+digest])
        self.tb('needs',self.a); dep.write_bytes(b'changed'); self.tb('needs',self.a,code=1)
        self.reseal(self.a,needs=['file /usr/lib/libfixture.so.1 >= 999'])
        shutil.rmtree(next((self.root/'var/lib/pacman/local').glob('library-*')))
        self.assertIn('version unknown',self.tb('needs',self.a).stdout)
        self.reseal(self.a,needs=['tam-bundle >= 99.0.0']); self.tb('needs',self.a,code=1)

    def test_runtime_disappears_after_prepare_and_anchor_status(self):
        dep=self.root_file('usr/lib/dependency')
        self.reseal(self.a,needs=['file /usr/lib/dependency']); self.install_a()
        self.reseal(self.b,needs=['file /usr/lib/new-dependency'])
        self.root_file('usr/lib/new-dependency')
        self.tb('install',self.b); dep.unlink()
        self.tb('status',code=4) # pending barrier takes precedence
        self.tb('apply'); self.tb('status',code=1) # retained A is now incompatible
        self.root_file('usr/lib/dependency')
        self.tb('status')
        self.tb('restore',self.a)
        dep.unlink(); self.tb('apply',code=4)
        self.assertEqual((self.root/'usr/lib/tamlinux/current').readlink().as_posix(),'version/'+self.id(self.b))

    def test_qml_and_isolated_python_probe(self):
        qmldir=self.root_file('usr/lib/qt6/qml/Example/Widgets/qmldir',b'module Example.Widgets\nThing 1.2 Thing.qml\n')
        python=self.root_file('usr/bin/python3',b'#!/bin/sh\n[ "$1" = -I ] && [ "$2" = -B ] && [ "$5" = good_module ]\n',0o755)
        self.reseal(self.a,needs=['qml Example.Widgets 1.1', 'python good_module'])
        self.tb('needs',self.a)
        qmldir.write_text('module Example.Widgets\n# Thing 99.0 fake.qml\n')
        self.tb('needs',self.a,code=1)
        qmldir.write_text('module Example.Widgets\ntypeinfo widgets.qmltypes\n')
        self.root_file('usr/lib/qt6/qml/Example/Widgets/widgets.qmltypes',b'Component { exports: ["Example.Widgets/Thing 1.2"] }\n')
        self.tb('needs',self.a)
        python.write_text('#!/bin/sh\nexit 1\n'); self.tb('needs',self.a,code=1)

    def test_elf_probe_refuses_malformed_missing_and_never_executes(self):
        # This executable would create a marker if accidentally run.
        source=self.base/'never.c'; source.write_text('#include <stdio.h>\nint main(void){puts("MUST-NOT-RUN"); return 0;}\n')
        exe=self.a/'placed/usr/bin/tam-demo'
        subprocess.run(['/usr/bin/gcc','-static',str(source),'-o',str(exe)],check=True)
        self.reseal(self.a,needs=['elf usr/bin/tam-demo']); r=self.tb('needs',self.a)
        self.assertNotIn('MUST-NOT-RUN',r.stdout+r.stderr)
        self.reseal(self.a,needs=['elf usr/bin/missing']); self.tb('needs',self.a,code=1)
        exe.write_bytes(b'\x7fELF'+b'\0'*100)
        self.reseal(self.a,needs=['elf usr/bin/tam-demo']); self.tb('needs',self.a,code=1)

    def test_production_tool_is_fully_static(self):
        data=(format2.BIN.parent/'tam-bundle').read_bytes()
        self.assertEqual(data[:6],b'\x7fELF\x02\x01')
        phoff=struct.unpack_from('<Q',data,32)[0]
        entsize,count=struct.unpack_from('<HH',data,54)
        types=[struct.unpack_from('<I',data,phoff+i*entsize)[0] for i in range(count)]
        self.assertNotIn(2,types); self.assertNotIn(3,types)

    def test_dynamic_elf_closure_is_inspected_and_missing_library_named(self):
        exe=self.a/'placed/usr/bin/tam-demo'; shutil.copy2('/usr/bin/true',exe)
        # Copy the known Arch x86_64 rootstock loader and libc into the fixture.
        for path in ['usr/lib/libc.so.6','usr/lib/ld-linux-x86-64.so.2']:
            self.root_file(path,Path('/'+path).read_bytes(),0o755)
        link=self.root/'lib64'; link.symlink_to('usr/lib')
        self.reseal(self.a,needs=['elf usr/bin/tam-demo'])
        self.tb('needs',self.a)
        (self.root/'usr/lib/libc.so.6').unlink()
        r=self.tb('needs',self.a,code=1); self.assertIn('libc.so.6',r.stderr)

    def test_interpreter_provided_by_selected_product(self):
        source=self.base/'interpreter.c'; source.write_text('int main(void){return 0;}\n')
        exe=self.a/'placed/usr/bin/tam-demo'
        subprocess.run(['/usr/bin/gcc',str(source),'-Wl,--dynamic-linker=/usr/lib/tamlinux/current/interpreter','-o',str(exe)],check=True)
        shutil.copy2('/usr/lib/ld-linux-x86-64.so.2',self.a/'tree/interpreter')
        for path in ['usr/lib/libc.so.6','usr/lib/ld-linux-x86-64.so.2']:
            self.root_file(path,Path('/'+path).read_bytes(),0o755)
        self.reseal(self.a,needs=['elf usr/bin/tam-demo'])
        self.tb('needs',self.a)
        (self.a/'tree/interpreter').unlink()
        self.reseal(self.a,needs=['elf usr/bin/tam-demo'])
        self.tb('needs',self.a,code=1)


if __name__=='__main__':
    enter_namespace(__file__)
    unittest.main(verbosity=2)
