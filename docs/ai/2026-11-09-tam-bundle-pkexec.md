# `tam-bundle` 0.3.0 and 0.3.1: privilege through `pkexec`

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
- **With the real pkexec**, after Fred's "Do that test now": two small
  bundles naming `user-units-reload`, in a probe slot under `/var/tmp`
  (root's), one administrator prompt each for switch, switch and back.
  Each made the right bundle live with root-owned files, the second
  reused its unchanged file by cloning, and `status` and `check` worked
  as the user. Each root run logged the step deferred, and the user's
  journal showed the user manager reloading at the same second, called
  by `systemctl` from the user's session. The probe slot and its state
  were then removed.
- **Gap found:** the user's run of a deferred step leaves no log line,
  because the log belongs to root.

## 0.3.1: recording the user's results

Six ways to close the gap were weighed: accept it and rely on the journal;
a results file per user in the state directory; a log in the user's own
state directory; a second pkexec call to write the line; the root run
dropping to the user to run the steps itself; and a pipe back, which
pkexec rules out by closing every descriptor but the first three. Fred:

> Go with option 2

- **`session.<uid>`:** when a root run defers a session step (pkexec) or
  leaves one for `tam-bundle after` (sudo, from `SUDO_UID`), it creates
  this file in the state directory, owned by that user, `0644`. The
  user's process appends its results there and never creates the file;
  without one, it says the result was not recorded. A user writes to the
  main log only where that log is theirs.
- **`status`** prints the last line of each `session.<uid>`.
- **Tests:** the user's result stays out of the root run's log, the file's
  owner and mode, `status`, and a run whose file was removed. All suites
  passed on tmpfs, Btrfs and ext4. The `sudo` path needs real root and is
  not covered by the tests.
