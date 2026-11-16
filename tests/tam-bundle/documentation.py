"""Optional documentation uses the same durable product transaction."""
from pathlib import Path
import os
import shutil
import unittest
from format2 import ProductTransactions
from pid_namespace import enter_namespace, require_namespace


def setUpModule():require_namespace()

class Documentation(unittest.TestCase):
    for name in ('setUp','tearDown','run_tb','tb','bundle','id','tx','install_a'):
        locals()[name]=getattr(ProductTransactions,name)

    def run_tb(self,*args,env=None,**kw):
        values=dict(env or {})
        if hasattr(self,'home'):values['HOME']=str(self.home)
        return ProductTransactions.run_tb(self,*args,env=values,**kw)

    def docs_bundle(self):
        product=self.base/'docs'
        shutil.copytree(self.b,product)
        (product/'BUNDLE').unlink();(product/'MANIFEST').unlink()
        (product/'SETTINGS').write_text('format 1\ncommon .bashrc\n')
        setting=product/'tree/defaults/home/.bashrc';setting.parent.mkdir(parents=True,exist_ok=True);setting.write_text('default bash\n')
        for name,leaf in [('info','bash.info'),('tldr','pages/common/tar.md'),('arch-wiki','html/en/Pacman.html')]:
            file=product/'placed/usr/share/doc'/name/leaf;file.parent.mkdir(parents=True,exist_ok=True);file.write_text('manual '+name)
        self.run_tb('seal',product,'--format','2','--name','demo','--version','docs',
                    '--optional-doc','info','--optional-doc','tldr','--optional-doc','archwiki')
        (self.root/'usr/share/tamlinux/rootstock/profile').write_text('format 1\nallow usr/bin\nallow usr/share/man\nallow usr/share/doc\n')
        return product

    def test_absent_then_selected_then_removed_same_bundle(self):
        product=self.docs_bundle()
        self.tb('install',product,'--docs','none');self.tb('apply');self.tb('check')
        self.assertFalse((self.root/'usr/share/doc/info/bash.info').exists())
        self.tb('docs','set','info,tldr');self.tb('apply');self.tb('check')
        self.assertTrue((self.root/'usr/share/doc/info/bash.info').is_file())
        self.assertTrue((self.root/'usr/share/doc/tldr/pages/common/tar.md').is_file())
        self.assertFalse((self.root/'usr/share/doc/arch-wiki/html/en/Pacman.html').exists())
        self.tb('docs','set','none');self.tb('apply');self.tb('check')
        self.assertFalse((self.root/'usr/share/doc/info/bash.info').exists())
        self.assertEqual((self.root/'usr/bin/tam-demo').read_text(),'#!/bin/sh\necho B\n')

    def test_detach_clears_selection_and_reinstall_works(self):
        product=self.docs_bundle();self.tb('install',product,'--docs','info,tldr');self.tb('apply')
        from format2 import ProductTransactions
        digest=ProductTransactions.bundle_hash(self,product)
        self.tb('detach',digest)
        self.assertIn('selected 0', (self.root/'var/lib/tamlinux/documentation').read_text())
        self.tb('install',product,'--docs','archwiki');self.tb('apply');self.tb('check')
        self.assertFalse((self.root/'usr/share/doc/info/bash.info').exists())
        self.assertTrue((self.root/'usr/share/doc/arch-wiki/html/en/Pacman.html').exists())

    def test_documentation_change_keeps_user_settings_and_session_agreement(self):
        product=self.docs_bundle()
        self.home=self.base/'home';self.home.mkdir()
        profile=self.root/'usr/share/tamlinux/rootstock/profile'
        helper=profile.parent/'reload';helper.write_text('#!/bin/sh\n[ "$1" = apply ] || exit 42\n');helper.chmod(0o755)
        profile.write_text(profile.read_text()+'user-step user-units-reload required usr/share/tamlinux/rootstock/reload\n')
        self.tb('install',product,'--docs','none');self.tb('apply')
        self.tb('prepare-settings');self.tb('apply-settings');self.tb('session-ready')
        personal=self.home/'.bashrc';personal.write_text('personal edit\n')
        self.tb('docs','set','info');self.tb('prepare-settings')
        import format2
        pkexec=self.base/'pkexec'
        pkexec.write_text('#!/bin/sh\nexec "'+str(format2.BIN)+'" --root "'+str(self.root)+'" apply "$2"\n')
        pkexec.chmod(0o755)
        self.tb('session-activate',env={'TAM_BUNDLE_PKEXEC':str(pkexec)})
        self.tb('session-ready');self.tb('check')
        self.assertEqual(personal.read_text(),'personal edit\n')

    def test_unowned_conflict_and_invalid_selection_refused(self):
        product=self.docs_bundle();p=self.root/'usr/share/doc/info/bash.info';p.parent.mkdir(parents=True);p.write_text('personal')
        self.tb('install',product,'--docs','info',code=1)
        self.assertEqual(p.read_text(),'personal')
        self.tb('install',product,'--docs','unknown',code=2)
        self.tb('install',self.a,'--docs','info',code=1)
        self.tb('install',product,'--docs','info,info',code=2)

    def test_matching_native_documentation_is_reused_and_never_removed(self):
        from runtime import Runtime
        product=self.docs_bundle()
        relative='usr/share/doc/arch-wiki/html/en/Pacman.html'
        path=self.root/relative;path.parent.mkdir(parents=True);path.write_text('manual arch-wiki')
        source=product/'placed/usr/share/doc/arch-wiki/SOURCE.json';source.write_text('{}')
        (product/'BUNDLE').unlink();(product/'MANIFEST').unlink()
        self.run_tb('seal',product,'--format','2','--name','demo','--version','docs',
                    '--optional-doc','info','--optional-doc','tldr','--optional-doc','archwiki')
        Runtime.package(self,'arch-wiki-docs','1-1',[relative])
        self.tb('install',product,'--docs','archwiki');self.tb('apply');self.tb('check')
        self.assertIn('provider archwiki arch-wiki-docs 1-1',(self.root/'var/lib/tamlinux/documentation').read_text())
        self.tb('docs','set','none');self.tb('apply');self.tb('check')
        self.assertEqual(path.read_text(),'manual arch-wiki')
        path.write_text('different native edition')
        self.tb('docs','set','archwiki',code=1)
        self.assertEqual(path.read_text(),'different native edition')

    def test_interrupted_documentation_change_finishes_forward(self):
        product=self.docs_bundle();self.tb('install',product,'--docs','none');self.tb('apply')
        self.tb('docs','set','info,tldr,archwiki')
        self.tb('apply',code=99,env={'TAM_BUNDLE_FAULT':'v2-placed-rename'})
        self.tb('apply');self.tb('check')
        self.assertTrue((self.root/'usr/share/doc/info/bash.info').exists())
        self.assertEqual(self.tx()['docs-to'],'7')

    def test_cancel_preserves_selection_and_retained_payload_is_checked(self):
        product=self.docs_bundle();self.tb('install',product,'--docs','info');self.tb('apply')
        receipt=(self.root/'var/lib/tamlinux/documentation').read_bytes()
        self.tb('docs','set','none');self.tb('cancel');self.tb('check')
        self.assertEqual((self.root/'var/lib/tamlinux/documentation').read_bytes(),receipt)
        kept=self.root/'usr/lib/tamlinux/version'/self.id(product)/'.placed/usr/share/doc/tldr/pages/common/tar.md'
        kept.write_text('corrupted omitted payload');self.tb('check',code=1)

def load_tests(loader, tests, pattern):
    return loader.loadTestsFromTestCase(Documentation)

if __name__=='__main__':
    enter_namespace(__file__)
    unittest.main()
