# `tam-bundle` 0.1.0: formats, switch, rollback and recovery

- **Date:** 2026-11-09
- **Stage:** Develop: the first slice of `tam-bundle`, per the approved
  [design](../plans/tam-bundle.md).
- **CLI Tool:** Claude Code 2.1.321
- **Model:** Claude Opus 5.5 (`claude-opus-5-5`)
- **Transcript:** Retained privately by the author.
- **Commits:** the commit that adds this record.
- **Authorship:**
  - Fred approved the design and asked for `tam-bundle` itself.
  - Claude wrote the code, the tests, the README and design updates, and
    this record.

## Guiding prompts

> I have pushed everything. Start writing tam-bundle itself in
> tamlinux-tools.

## What changed

- **`src/tam-bundle/`**, one file per concern:
  - `meta.c`: `BUNDLE` and `MANIFEST` parsing and validation, the bundle
    hash and id;
  - `tree.c`: building a manifest from a tree, checking a tree, the stat
    cache;
  - `slot.c`: slots, the lock, the intent file, the keep list, the log, and
    tree removal;
  - `switch.c`: staging, switch, back and interruption recovery;
  - `after.c`: post-install steps and the native-needs check;
  - `fault.c`: fault points, active only in the test build;
  - `main.c`: the command line.
- **Commands:** `seal`, `verify`, `needs`, `switch`, `back`, `recover`,
  `status`, `check`, `where`, `keep`, `unkeep`, `prune` and `after`.
- **Additions to the design,** recorded in it:
  - `seal`, the last step of the coming `build` and how tests make
    bundles;
  - `after`, to run post-install steps as the desktop user after a root
    switch;
  - the stat cache also records change time;
  - user state follows `libtam`'s `$XDG_STATE_HOME/tamlinux/tam-bundle/`;
  - bundle files are opened with `openat2` (`RESOLVE_BENEATH`,
    `RESOLVE_NO_SYMLINKS`).
- **Not yet:**
  - `build` and `lock`;
  - privilege through `pkexec` (a non-writable slot is refused with
    advice);
  - the `man-index` step (reported as skipped);
  - the man page and `tam` pages;
  - the native adapter package.

## Verification

- `make test` passes: the existing `tam-file-select` tests and
  `tests/tam-bundle/run.sh` against the sanitizer test build.
- `tests/tam-bundle/run.sh` passed on tmpfs, Btrfs and ext4. It covers:
  - seal and verify;
  - identical hashes for identical trees;
  - refusal of tampered files, a newer format, links leaving the tree,
    unsafe paths, unknown steps, another host, missing needs, an
    unwritable slot, and a link swapped into a bundle after sealing;
  - switch, back and switching to a kept bundle;
  - edits, refused and then saved;
  - prune and keep;
  - the lock;
  - clones of unchanged files on Btrfs.
- **Older-bundle test:** switched through three bundles, then rolled back
  to each and verified each in full.
- **Interruption test:**
  - 11 fault points (7 in switch, 4 in back) each left exactly one whole
    bundle live, and `recover` cleaned up;
  - 30 random `SIGKILL`s during slowed switches each recovered.
- **The installed build** links `libtam` statically and contains no fault
  points.
- **Not tested:** running as root, and a slot whose `current/` is on
  another filesystem.
