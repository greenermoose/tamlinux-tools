# tam-bundle 0.5.0: recipe targets and pinned packaging files

- **CLI Tool:** Codex CLI 0.167.0
- **Model:** `gpt-6.1-sol`
- **Transcript:** Retained privately by the author.
- **Commits:** The commit adding this record, and subsequent fixes naming it.
- **Authorship:** Codex implemented the already approved builder additions.

## Guiding prompt

> Pick up where claude left off and work on tam-bundle. The goal is to finish tam-bundle and start using it on [redacted: host].

## Changes

- Recipes accept lists of make and install targets, with the existing defaults.
  Options and variable assignments cannot masquerade as targets.
- Bundle `file` entries read regular data files from the packaging commit.
  The recipes and lock must match that commit. `BUNDLE` records the packaging
  source, and dirty notes cannot alter a bundle built from it.
- File destinations cannot traverse links, replace component output or replace
  bundle metadata. Files remain hashed and checked with the rest of the tree.
- Separate install targets put `tam-file-select` in the product bundle and
  keep the rollback tool, manual and polkit action in the native adapter.

## Verification

The complete `make test` passed, including address/undefined-behaviour
sanitizers, interruption recovery, older-bundle rollback, builder and manual
checks. Builder checks also passed on Btrfs. New cases exercise multiple
targets, changed packaging commits, dirty inputs, missing and linked sources,
destination collisions, traversal and reserved metadata names. No product
slot or running session was changed by these checks.

## Earlier design continuation

Commit `9f02508` was written by Claude Code 2.1.321 using
`claude-opus-5-5`. Its retained transcript confirms the decisions to use a
`user@.service` environment drop-in and a package-owned login profile file.
This forward record supplies the missing session documentation; the earlier
commit is unchanged.
