# Upstream sources and field surveys

`tamlinux-tools` is original C code written for Tamlinux against
[`libtam`](https://github.com/greenermoose/libtam). It is not a clone or
fork of another project, and no third-party source is copied into it.

## Code ancestry

None. If a tool later adapts third-party source, record here the project,
exact version or commit, license and the files affected.

## Idea sources

- **`tam-file-select`:** the XDG Desktop Portal `FileChooser` interface,
  used through `libtam-portal`.
- **`tam-bundle` (planned):** design ideas, not code, from:
  - OSTree (staged deployments, content addressing)
  - pacman/libalpm (record after install, three-hash handling of
    user-edited files)
  - yadm (home directory as a git work tree)
  - Nix (exact pins, generations, rollback)

  See the survey below.

## Survey records

- [2026-11-09 — file bundles, install records and atomic
  switching](upstream/2026-11-09-file-bundles-and-atomic-switch.md):
  OSTree, pacman, yadm, GNU Stow, systemd-sysext and a review of Nix's
  ideas, for `tam-bundle`.
  - **Outcome:** proposals only. Real files staged beside the live tree,
    one `renameat2(RENAME_EXCHANGE)`, reflinked unchanged files, the
    record written last, and an interruption test.
