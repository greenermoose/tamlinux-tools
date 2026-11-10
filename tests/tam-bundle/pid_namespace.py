"""Run signal tests as PID 1 with a fresh /proc; never fall back to the host."""
import os
from pathlib import Path
import subprocess
import sys


def require_namespace():
    # A PID namespace without remounting /proc would still expose host PID
    # numbers. Verify both the caller's PID and the procfs view of that PID.
    if os.getpid() != 1 or Path('/proc/self/stat').read_text().split()[0] != '1':
        raise RuntimeError('signal tests require a private PID namespace and /proc')
    if os.readlink('/proc/self/ns/pid') != os.readlink('/proc/1/ns/pid'):
        raise RuntimeError('/proc belongs to a different PID namespace')


def enter_namespace(script):
    if os.getpid() == 1:
        require_namespace()
        return
    # No environment flag bypasses this guard. Failure to unshare is a
    # test failure, never permission to try the suite on host processes.
    command = ['/usr/bin/unshare', '--user', '--map-root-user', '--mount',
               '--pid', '--fork', '--mount-proc', '--kill-child=KILL', '--',
               sys.executable, '-B', str(Path(script).resolve()), *sys.argv[1:]]
    raise SystemExit(subprocess.run(command, check=False).returncode)
