#!/usr/bin/env bash
# The manual page: well-formed, and it names every command and option that
# `tam-bundle --help` lists, so the two cannot drift apart unnoticed.
set -euo pipefail

PAGE=${1:-man/tam-bundle.1}
BIN=./bin/tam-bundle
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

# The page's text with roff escapes for hyphens undone.
text=$(sed 's/\\-/-/g' "$PAGE")
help=$("$BIN" --help)

# Commands: the first word of each indented line under "Commands:".
commands=$(printf '%s\n' "$help" | sed -n '/^Commands:/,/^$/{/^  [a-z]/{s/^  \([a-z][a-z-]*\).*/\1/;p}}')
# Options: every --word or -o in the help text.
options=$(printf '%s\n' "$help" | grep -oE '(^|[ [])(--[a-z-]+|-[a-z])\b' | tr -d ' [' | sort -u)

missing=""
for c in $commands; do
    grep -qE "^\.(B|BI|BR) $c( |$)" <<<"$text" || missing="$missing command:$c"
done
for o in $options; do
    grep -qF -- "$o" <<<"$text" || missing="$missing option:$o"
done
if [ -n "$missing" ]; then
    echo "  [FAIL] $PAGE does not describe:$missing" >&2
    fail=1
else
    echo "  [PASS] $PAGE describes every command and option in --help"
fi
exit $fail
