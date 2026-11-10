# Design for `tam-bundle`

- **Date:** 2026-11-09
- **Stage:** Plan: the design document for `tam-bundle`, reviewed before
  any code.
- **CLI Tool:** Claude Code 2.1.321
- **Model:** Claude Opus 5.5 (`claude-opus-5-5`)
- **Transcript:** Retained privately by the author.
- **Commits:** the commit that adds this record.
- **Authorship:**
  - Fred set the requirements, the command name, this repository as its
    home, and the scope the design had to cover.
  - Claude ran the exchange probe and wrote the design.
  - Fred reviewed the design without edits.
- **Checked against:**
  - A C probe of `renameat2(RENAME_EXCHANGE)` and `FICLONE` on Btrfs and
    ext4, Linux 7.2.
  - The `systemd.unit(5)` user unit search path.
  - `libtam`'s `tam_fs_write_atomic`, which syncs the file but not its
    directory.
  - The size and file count of the current Tamlinux commands output.

## Guiding prompts

> Start the tam-bundle design draft, plan 53 step 3.

> Looks great. Commit. Next step?

## Key decisions

- **One slot, one live path:** everything installs under
  `/usr/lib/tamlinux/current/`; kept trees sit beside it, so every
  exchange stays on one filesystem.
- **Identity inside the tree:** `BUNDLE` and `MANIFEST` change in the same
  exchange as the files, and with an intent file decide every interruption
  case.
- **Rollback without copying:** a kept tree is exchanged straight back.
- **Survey element 5 narrowed:** fixed paths belong to the native adapter
  package, so only hand edits inside the live tree need handling; a switch
  stops on them and `--save-edits` saves them.

## Verification

- Documentation only; no code in the repository changed.
- The exchange probe passed every check on both filesystems; reflinks are
  supported on Btrfs and not on ext4. The probe source was not committed.
- Items not yet verified are listed as open in the design, for example
  whether the user service manager finds units through `XDG_DATA_DIRS`.
