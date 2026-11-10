#!/usr/bin/env python3
"""Package identity proofs with a private mount of a synthetic native DB."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import format2
from pid_namespace import enter_namespace, require_namespace


def setUpModule():
    require_namespace()


class BuildEnvironment(unittest.TestCase):
    def test_only_selected_native_versions_invalidate_cache(self):
        with tempfile.TemporaryDirectory(prefix='tb-buildenv-',dir='/tmp') as temp:
            base=Path(temp); db=base/'pacman'; local=db/'local'; local.mkdir(parents=True)
            (local/'ALPM_DB_VERSION').write_text('9\n')
            packages=['gcc','binutils','glibc','linux-api-headers','make','pkgconf']
            real=subprocess.run(['/usr/bin/pacman','-Q',*packages],capture_output=True,text=True,check=True)
            versions=dict(line.split(' ',1) for line in real.stdout.splitlines())
            def package(name,version):
                for p in local.glob(name+'-*'):shutil.rmtree(p)
                p=local/(name+'-'+version); p.mkdir()
                (p/'desc').write_text(f'%NAME%\n{name}\n\n%VERSION%\n{version}\n\n%DESC%\nfixture\n\n%ARCH%\nx86_64\n\n')
                (p/'files').write_text('%FILES%\n\n')
            for name,version in versions.items():package(name,version)
            package('selected','1.0-1'); package('unrelated','1.0-1')
            repo=base/'repo'; repo.mkdir()
            (repo/'Makefile').write_text('all:\n\t@true\ninstall:\n\tinstall -d $(DESTDIR)$(PREFIX)/share\n\techo payload > $(DESTDIR)$(PREFIX)/share/value\n')
            env=dict(os.environ,GIT_CONFIG_GLOBAL='/dev/null',GIT_CONFIG_NOSYSTEM='1',
                GIT_AUTHOR_NAME='test',GIT_AUTHOR_EMAIL='test@example.invalid',
                GIT_COMMITTER_NAME='test',GIT_COMMITTER_EMAIL='test@example.invalid',XDG_CACHE_HOME=str(base/'cache'))
            for args in [('init','-q',str(repo)),('-C',str(repo),'add','Makefile'),('-C',str(repo),'commit','-qm','fixture')]:
                subprocess.run(['/usr/bin/git',*args],env=env,check=True)
            commit=subprocess.check_output(['/usr/bin/git','-C',str(repo),'rev-parse','HEAD'],env=env,text=True).strip()
            recipes=base/'recipes'; recipes.mkdir()
            (recipes/'bundle').write_text('format 1\nname environment\n')
            (recipes/'demo.recipe').write_text('format 1\ncomponent demo\nbuild make\nbuild-needs selected\n')
            lock=base/'lock'; lock.write_text(f'format 1\ncomponent demo {repo} {commit}\n')
            # enter_namespace already established private user/mount/PID
            # namespaces. Refuse to mount anything before that assertion.
            require_namespace()
            subprocess.run(['/usr/bin/mount','--bind',str(db),'/var/lib/pacman'],check=True)
            try:
                def build(label):
                    output=base/label
                    r=subprocess.run([str(format2.BIN),'--slot',str(base/'slot'),'build',str(recipes),str(lock),
                        '-o',str(output),'--version','1'],env=env,capture_output=True,text=True,timeout=30)
                    self.assertEqual(r.returncode,0,r.stdout+r.stderr)
                    metadata='\n'.join(line for line in (output/'BUNDLE').read_text().splitlines() if not line.startswith('built '))
                    return metadata,r.stdout,(output/'recipe/BUILDENV').read_text()
                first,_,record=build('first'); self.assertIn('package selected 1.0-1',record)
                self.assertNotIn('unrelated',record)
                package('unrelated','2.0-1')
                second,log,_=build('second'); self.assertIn('from the build cache',log)
                self.assertEqual(first,second)
                package('selected','2.0-1')
                third,log,_=build('third'); self.assertIn('building demo',log)
                self.assertNotEqual(first,third)
                package('gcc',versions['gcc']+'.1')
                fourth,log,_=build('fourth'); self.assertIn('building demo',log)
                self.assertNotEqual(third,fourth)
            finally:
                subprocess.run(['/usr/bin/umount','/var/lib/pacman'],check=True)


if __name__=='__main__':
    enter_namespace(__file__)
    unittest.main(verbosity=2)
