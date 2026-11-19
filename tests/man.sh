#!/usr/bin/env bash
# Check that installed manual pages format cleanly with groff.
set -euo pipefail

PAGE=${1:-man/tam-journal.1}
fail=0

if command -v groff >/dev/null; then
    warnings=$(groff -man -ww -z "$PAGE" 2>&1)
    if [ -n "$warnings" ]; then
        printf '  [FAIL] groff warns about %s:\n%s\n' "$PAGE" "$warnings" >&2
        fail=1
    else
        echo "  [PASS] $PAGE formats without warnings"
    fi
else
    echo "  [SKIP] groff is not installed; $PAGE not formatted"
fi

exit $fail
