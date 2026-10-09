#!/usr/bin/env bash
set -euo pipefail

BIN="./bin/tam-file-select"

if [ ! -x "$BIN" ]; then
    echo "Error: Binary $BIN not found or not executable." >&2
    exit 1
fi

echo "Running tamlinux-tools test suite..."

# Test 1: Binary exists and links to libtam
echo "  [TEST] Link verification"
ldd "$BIN" | grep -q "libtam.so" || {
    echo "  [FAIL] $BIN does not link to libtam.so" >&2
    exit 1
}
echo "  [PASS] Links dynamically against libtam.so"

# Test 2: Reject unknown option with exit code 2 and proper error message
echo "  [TEST] Unknown option rejection (--bogus)"
set +e
ERR_OUT=$("$BIN" --bogus 2>&1 >/dev/null)
RET=$?
set -e

if [ "$RET" -ne 2 ]; then
    echo "  [FAIL] Expected exit code 2, got $RET" >&2
    exit 1
fi
if [[ "$ERR_OUT" != *"tam-file-select: unknown option --bogus"* ]]; then
    echo "  [FAIL] Unexpected stderr: $ERR_OUT" >&2
    exit 1
fi
echo "  [PASS] Exit code 2 and stderr message on unknown option"

# Test 3: Reject --help option (adhering to add_help=False parity)
echo "  [TEST] --help option rejection"
set +e
ERR_OUT=$("$BIN" --help 2>&1 >/dev/null)
RET=$?
set -e

if [ "$RET" -ne 2 ]; then
    echo "  [FAIL] Expected exit code 2 for --help, got $RET" >&2
    exit 1
fi
if [[ "$ERR_OUT" != *"tam-file-select: unknown option --help"* ]]; then
    echo "  [FAIL] Unexpected stderr for --help: $ERR_OUT" >&2
    exit 1
fi
echo "  [PASS] Exit code 2 and stderr message on --help"

# Test 4: Missing option arguments
echo "  [TEST] Missing argument for --title"
set +e
ERR_OUT=$("$BIN" --title 2>&1 >/dev/null)
RET=$?
set -e

if [ "$RET" -ne 2 ]; then
    echo "  [FAIL] Expected exit code 2 for missing --title argument, got $RET" >&2
    exit 1
fi
echo "  [PASS] Exit code 2 on missing --title argument"

echo "All tamlinux-tools tests passed successfully!"
