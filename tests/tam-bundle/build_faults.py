#!/usr/bin/env python3
"""Cache and built-bundle publication interruption tests; private PID/proc."""
import os
from pathlib import Path
import random
import shutil
import subprocess
import tempfile
import time
import unittest
import format2
from pid_namespace import enter_namespace, require_namespace

def setUpModule():
    require_namespace()

class BuildFaults(unittest.TestCase):
    def test_cache_and_output_interruption(self):
        with tempfile.TemporaryDirectory(prefix='tb-build-fault-',dir='/tmp') as temp:
            base=Path(temp); repo=base/'repo'; repo.mkdir(); recipes=base/'recipes'; recipes.mkdir()
            (repo/'Makefile').write_text('all:\n\t@true\ninstall:\n\tinstall -d $(DESTDIR)$(PREFIX)/share\n\techo complete > $(DESTDIR)$(PREFIX)/share/value\n')
            env=dict(os.environ,GIT_CONFIG_GLOBAL='/dev/null',GIT_CONFIG_NOSYSTEM='1',
                GIT_AUTHOR_NAME='test',GIT_AUTHOR_EMAIL='test@example.invalid',
                GIT_COMMITTER_NAME='test',GIT_COMMITTER_EMAIL='test@example.invalid',XDG_CACHE_HOME=str(base/'cache'))
            for args in [('init','-q',str(repo)),('-C',str(repo),'add','Makefile'),('-C',str(repo),'commit','-qm','fixture')]:
                subprocess.run(['/usr/bin/git',*args],env=env,check=True)
            commit=subprocess.check_output(['/usr/bin/git','-C',str(repo),'rev-parse','HEAD'],env=env,text=True).strip()
            (recipes/'bundle').write_text('format 1\nname fault\n')
            (recipes/'demo.recipe').write_text('format 1\ncomponent demo\nbuild make\n')
            lock=base/'lock'; lock.write_text(f'format 1\ncomponent demo {repo} {commit}\n')
            output=base/'output'; trace=base/'trace'
            argv=[str(format2.BIN),'--slot',str(base/'slot'),'build',str(recipes),str(lock),'-o',str(output),'--version','1']
            def run(values=None,code=0):
                r=subprocess.run(argv,env=dict(env,**(values or {})),capture_output=True,text=True,timeout=30)
                self.assertEqual(r.returncode,code,r.stdout+r.stderr)
                self.assertNotIn('runtime error:',r.stderr); self.assertNotIn('ERROR: AddressSanitizer',r.stderr)
            def reset():
                for p in [output,base/'output.partial',base/'cache']:
                    if p.exists():shutil.rmtree(p)
            def recover():
                if not output.exists():run()
                check=subprocess.run([str(format2.BIN),'verify',str(output)],env=env,capture_output=True,text=True,timeout=30)
                self.assertEqual(check.returncode,0,check.stdout+check.stderr)
                self.assertEqual((output/'share/value').read_text(),'complete\n')
                metadata=dict(line.split(' ',1) for line in (output/'BUNDLE').read_text().splitlines())
                return metadata['hash']
            run({'TAM_BUNDLE_TRACE':str(trace)}); expected=recover()
            count=len(trace.read_text().splitlines()); self.assertEqual(count,7)
            for point in range(1,count+1):
                with self.subTest(point=point):
                    reset(); run({'TAM_BUNDLE_FAULT_AT':str(point)},99)
                    self.assertEqual(recover(),expected)
            rng=random.Random(808)
            for _ in range(12):
                reset(); p=subprocess.Popen(argv,env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
                time.sleep(rng.uniform(.001,.04))
                if p.poll() is None:p.kill()
                p.wait(); self.assertEqual(recover(),expected)
            print(f'  checked {count} cache/build publication events and 12 randomized kills')

if __name__=='__main__':
    enter_namespace(__file__)
    unittest.main(verbosity=2)
