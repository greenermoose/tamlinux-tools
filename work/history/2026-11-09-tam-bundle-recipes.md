# tam-bundle 0.5.0–0.5.2: recipes and native bring-up

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
- 0.5.1 corrects planning on a new or root-owned slot: dry runs take a shared
  read-only lock when present and never create state. A first switch can be
  planned before either the slot or its state exists. Native bring-up
  exposed the missing-state case; new tests cover it and a read-only lock.
- 0.5.2 makes changing commands set the state lock to 0644, matching the
  public-state contract. The real root-owned slot exposed libtam's private
  0600 default; the next privileged change repairs old locks too. The test
  suite now checks the default mode before testing read-only planning.

## Verification

The complete `make test` passed, including address/undefined-behaviour
sanitizers, interruption recovery, older-bundle rollback, builder and manual
checks. Builder checks also passed on Btrfs. New cases exercise multiple
targets, changed packaging commits, dirty inputs, missing and linked sources,
destination collisions, traversal and reserved metadata names. No product
slot or running session was changed by these checks.

Native bring-up subsequently installed 0.5.2 through the recorded native
adapter and built a full development bundle. On the real product slot:
initial install, switch to updated readers, `check --full`, rollback to
the older bundle and forward switch all passed. Rollback/forward exchanges
took 0.1 seconds. The updated switch reused 2,624 unchanged files and copied
nine. The unprivileged dry-run plan works with the root-owned 0644 lock.
The previous desktop service stayed on its existing executable and PID.
The superseded tools development install was removed after verification.

## Earlier design continuation

Commit `9f02508` was written by Claude Code 2.1.321 using
`claude-opus-5-5`. Its retained transcript confirms the decisions to use a
`user@.service` environment drop-in and a package-owned login profile file.
This forward record supplies the missing session documentation; the earlier
commit is unchanged.
