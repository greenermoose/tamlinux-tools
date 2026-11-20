# tamlinux-tools roadmap

`tamlinux-tools` holds small, standalone C utilities linked statically
against `libtam`. `VERSION` tracks the suite version; individual tools
may also report their own version with `--version`.

## Current tools

- **`tam-file-select`:** native desktop file chooser delegating to the
  XDG Desktop Portal through `libtam-portal`.
- **`tam-journal` (0.1.0):** plain-text daily journal in Micro with
  private drafts, per-day locks, and atomic replacement
  (`docs/plans/plain-text-journal.md`).

## Retired

- **`tam-bundle`:** retired now that Tamlinux installs directly from
  pinned source builds; its source, tests, manual page, and design plan
  were removed.

## Queued work

No tools work is queued here. New utilities or changes enter from
Tamlinux candidates on Fred's word.
