# `tam-bundle` 0.3.0: privilege through `pkexec`

- **Date:** 2026-11-09
- **Stage:** Develop: the privilege part of `tam-bundle`, per the approved
  [design](../plans/tam-bundle.md).
- **CLI Tool:** Claude Code 2.1.321
- **Model:** Claude Opus 5.5 (`claude-opus-5-5`)
- **Transcript:** Retained privately by the author.
- **Commits:** the commit that adds this record.
- **Authorship:**
  - Fred asked for the next part of the work.
  - Claude wrote the code, the tests, the polkit action, the README and
    design updates, and this record.

## Guiding prompts

> Continue plan 53 step 4: pkexec privilege for tam-bundle

## What changed

- **New `src/tam-bundle/elevate.c`:** a user's changing command on a
  root-owned slot runs again through `/usr/bin/pkexec`, by this program's
  absolute path, with relative bundle and state paths made absolute
  (pkexec starts in `/root` with a cleared environment). It reports
  pkexec's own failures (`126` refused, `127` could not run, pkexec
  missing) with what to do instead.
- **Session steps hand-over (`after.c`):** `user-units-reload`,
  `qml-cache-purge` and `shell-restart` are session steps. The root run
  under pkexec logs them `deferred to uid <uid>`; the waiting user process
  runs the steps deferred to its uid from the log lines added during the
  run. Run as root without pkexec, they still print a note to run
  `tam-bundle after`.
- **Readable state (`slot.c`, `tree.c`):** state directories `0755`, the
  intent and stat files `0644`, so `status`, `check` and `where` need no
  privilege, as the design's command table says, and the user process can
  read the log.
- **`data/org.tamlinux.bundle.policy.in`:** the polkit action
  `org.tamlinux.bundle` for the native adapter to install;
  `make install` writes it under `share/polkit-1/actions/` with the
  installed path, and `make test` checks it is well-formed when `xmllint`
  is present.
- **Version:** 0.3.0.

## Verification

- `make test` passes, and both bundle test scripts passed on tmpfs, Btrfs
  and ext4.
- New tests in `tests/tam-bundle/run.sh` use a stand-in for pkexec that
  only the test build accepts (`$TAM_BUNDLE_PKEXEC`). They cover switch,
  back and keep through it; relative paths reaching it as absolute ones;
  each session step deferred by the root run and run once by the user; no
  pkexec for a dry run or `status`; refusals for `126`, `127` and a
  missing pkexec, leaving the slot untouched; a hand-run `pkexec`
  deferring and `after` running the step; readable state modes; and the
  installed build having no stand-in setting.
- A staged `make install DESTDIR=... PREFIX=/usr` gave the policy with
  `exec.path` `/usr/bin/tam-bundle`, and `make uninstall` removed it.
- Not yet tried with the real pkexec and a root-owned slot: that writes
  under `/var/lib` and waits for Fred's approval.
