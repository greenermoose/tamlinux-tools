# `tam-bundle`: the manual page

- **Date:** 2026-11-09 (UTC 2026-11-10)
- **Stage:** Develop: the man page that the approved
  [design](../plans/tam-bundle.md) requires (requirement 10).
- **CLI Tool:** Claude Code 2.1.321
- **Model:** Claude Opus 5.5 (`claude-opus-5-5`)
- **Transcript:** Retained privately by the author.
- **Commits:** the commit that adds this record.
- **Authorship:**
  - Fred asked for the work.
  - Claude wrote the page, the check, the Makefile, README and design
    updates, and this record.
- **Checked against:** `src/tam-bundle/*.c` and `tb.h` (commands, options,
  exit codes, state and cache paths, recovery, privilege, post-install
  steps), `tam-bundle --help`, the design, and runs of the page's first
  example in a temporary slot.

## Guiding prompt

> Continue plan 53 step 4: the man page and tam pages.

## What changed

- **New `man/tam-bundle.1`:** every command and option, privilege through
  `pkexec` and `sudo`, the post-install steps, the slot, state and cache
  files, the record formats, environment, exit status and examples.
  `--help` already pointed readers at `tam-bundle(1)`.
- **`make install`** installs it to `$(MANDIR)/man1` (default
  `$(PREFIX)/share/man`); `make uninstall` removes it.
- **New `tests/man.sh`, run by `make test`:** the page formats with
  `groff -man -ww` without warnings, and it names every command and option
  that `--help` lists.
- **Found while checking the text:** the design says every command runs
  the recovery check first. In the code, `switch` and `back` recover
  before they start, `status` reports an interrupted switch with exit 4,
  and the other commands do not check. The page describes the code.
- **No version bump.** The program is unchanged, and `BUNDLE` records
  `tool tam-bundle <version>`, which is part of the bundle hash, so a
  documentation-only bump would change every bundle's hash.

## Verification

- `make test` passes, including the new check.
- The check fails when an option is removed from a copy of the page.
- The first example (two versions, switch, switch, back, status) ran as
  written in a temporary slot, and `check` matched afterwards.
- `make install` and `make uninstall` into a temporary prefix installed
  and removed the page.
