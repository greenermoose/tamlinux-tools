#!/usr/bin/env python3
"""Prove that the signal-test namespace cannot obtain a host pidfd."""
import os
from pathlib import Path
import subprocess
import sys

from pid_namespace import require_namespace


if len(sys.argv) == 3 and sys.argv[1] == '--child':
    require_namespace()
    outside = int(sys.argv[2])
    assert not Path(f'/proc/{outside}').exists(), 'host process visible in private /proc'
    try:
        fd = os.pidfd_open(outside)
    except ProcessLookupError:
        pass
    else:
        os.close(fd)
        raise AssertionError('host PID is addressable from signal-test namespace')
else:
    # This is the only test intentionally outside the signal namespace.
    # It starts its own sentinel and never runs a bundle scanner here.
    sentinel = subprocess.Popen(['/usr/bin/sleep', '60'])
    try:
        command = ['/usr/bin/unshare', '--user', '--map-root-user', '--mount',
                   '--pid', '--fork', '--mount-proc', '--kill-child=KILL', '--',
                   sys.executable, '-B', str(Path(__file__).resolve()), '--child', str(sentinel.pid)]
        subprocess.run(command, check=True, timeout=15)
        assert sentinel.poll() is None, 'outside sentinel was disturbed'
        print('  [PASS] signal-test namespace hides outside processes and refuses their pidfds')
    finally:
        if sentinel.poll() is None:
            sentinel.kill()
        sentinel.wait(timeout=5)
