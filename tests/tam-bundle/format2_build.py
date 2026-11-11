#!/usr/bin/env python3
"""Build, verify, archive and apply a conventional recipe in temporary roots."""
import hashlib
import os
from pathlib import Path
import subprocess
import unittest
import format2
from pid_namespace import enter_namespace, require_namespace

def setUpModule():
    require_namespace()

class Format2Build(unittest.TestCase):
    bundle=format2.ProductTransactions.bundle
    id=format2.ProductTransactions.id
    run_tb=format2.ProductTransactions.run_tb
    tb=format2.ProductTransactions.tb
    setUp=format2.ProductTransactions.setUp
    tearDown=format2.ProductTransactions.tearDown

    def setup_recipe(self,extras=''):
        repo=self.base/'repo'; repo.mkdir()
        (repo/'Makefile').write_text('''all:
	@test "$(PREFIX)" = /usr
install:
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/lib/tamlinux/current/share $(DESTDIR)$(PREFIX)/share/man/man1
	printf '#!/bin/sh\\necho hi\\n' > $(DESTDIR)$(PREFIX)/bin/tam-demo
	chmod 755 $(DESTDIR)$(PREFIX)/bin/tam-demo
	echo data > $(DESTDIR)$(PREFIX)/lib/tamlinux/current/share/info
	ln -s info $(DESTDIR)$(PREFIX)/lib/tamlinux/current/share/link
	echo manual > $(DESTDIR)$(PREFIX)/share/man/man1/tam-demo.1
'''+extras)
        env=dict(os.environ,GIT_CONFIG_GLOBAL='/dev/null',GIT_CONFIG_NOSYSTEM='1',
            GIT_AUTHOR_NAME='test',GIT_AUTHOR_EMAIL='test@example.invalid',
            GIT_COMMITTER_NAME='test',GIT_COMMITTER_EMAIL='test@example.invalid',
            XDG_CACHE_HOME=str(self.base/'cache'))
        for args in [('init','-q',str(repo)),('-C',str(repo),'add','Makefile'),('-C',str(repo),'commit','-qm','fixture')]:
            subprocess.run(['/usr/bin/git',*args],env=env,check=True)
        commit=subprocess.check_output(['/usr/bin/git','-C',str(repo),'rev-parse','HEAD'],env=env,text=True).strip()
        recipes=self.base/'recipes'; recipes.mkdir()
        (recipes/'bundle').write_text('format 2\nname workstation\nplace usr/bin\nplace usr/share/man\ntarget arch x86_64 baseline x86-64-v2\nneeds command sh\nneeds tam-bundle >= 0.8.0\n')
        (recipes/'demo.recipe').write_text('format 1\ncomponent demo\nbuild make\n')
        lock=self.base/'lock'; lock.write_text(f'format 1\ncomponent demo {repo} {commit}\n')
        return recipes,lock,env

    def test_portable_output_and_exact_build_inputs(self):
        recipes,lock,env=self.setup_recipe()
        first=self.base/'built'; second=self.base/'built-again'
        self.run_tb('build',recipes,lock,'-o',first,'--version','1','--built','2026-11-10T00:00:00Z',env=env)
        self.run_tb('build',recipes,lock,'-o',second,'--version','1','--built','2026-11-10T00:00:00Z','--verify',env=env)
        self.assertEqual(self.id(first),self.id(second))
        self.assertEqual((first/'tree/recipe/tamlinux.lock').read_bytes(),lock.read_bytes())
        self.assertEqual((first/'tree/recipe/bundle').read_bytes(),(recipes/'bundle').read_bytes())
        self.assertEqual((first/'tree/recipe/demo.recipe').read_bytes(),(recipes/'demo.recipe').read_bytes())
        self.assertIn('buildenv sha256:'+hashlib.sha256((first/'tree/recipe/BUILDENV').read_bytes()).hexdigest(),(first/'BUNDLE').read_text())
        self.assertFalse((first/'tree/bin/tam-demo').exists())
        self.assertEqual((first/'tree/share/link').readlink().as_posix(),'info')
        exe=first/'placed/usr/bin/tam-demo'; self.assertTrue(exe.is_file() and not exe.is_symlink())
        archive=self.base/'workstation.tbundle'; self.run_tb('pack',first,'-o',archive); self.run_tb('verify',archive)
        second_archive=self.base/'second.tbundle'
        self.run_tb('pack',second,'-o',second_archive)
        self.assertEqual(archive.read_bytes(),second_archive.read_bytes())
        shell=self.root/'usr/bin/sh'; shell.parent.mkdir(parents=True,exist_ok=True); shell.write_text('fixture'); shell.chmod(0o755)
        self.tb('install',archive); self.tb('apply'); self.tb('check')
        self.assertEqual((self.root/'usr/bin/tam-demo').read_bytes(),exe.read_bytes())
        self.assertTrue((self.root/'usr/share/man/man1/tam-demo.1').is_file())
        self.assertEqual((self.root/'usr/lib/tamlinux/current/share/info').read_text(),'data\n')

    def test_undeclared_placement_refuses_without_publication(self):
        extra='\tinstall -d $(DESTDIR)/usr/local/bin\n\ttouch $(DESTDIR)/usr/local/bin/foreign\n'
        recipes,lock,env=self.setup_recipe(extra)
        out=self.base/'refused'
        r=self.run_tb('build',recipes,lock,'-o',out,'--version','1',code=1,env=env)
        self.assertIn('/usr/local',r.stderr); self.assertFalse(out.exists())

if __name__=='__main__':
    enter_namespace(__file__)
    unittest.main(verbosity=2)
