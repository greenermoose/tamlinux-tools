# tam-bundle 0.7 settings audit and format review

- **Date**: 2026-11-10
- **CLI Tool**: Codex CLI (`codex`) `0.167.0`
- **Model**: `gpt-6.1-sol`
- **Transcript**: Retained privately by the author.
- **Authorship**: AI-assisted implementation, verification and documentation,
  continuing `9be1c5e`. Concrete format acceptance remains with the developer.
- **Checked against**: `src/tam-bundle/settings.c`, `format2.c`, `util.c`,
  `tests/tam-bundle/settings.py`, the public design and the full `make test` run.

## Guiding prompt (excerpts)

> Finish the existing 0.7 draft: audit parsers, metadata bounds, transaction agreement and activation-profile changes during retry; extend fault coverage to capture, prepare and import/export; then present the concrete formats for my review. Continue afterward through 0.8 and the remaining migration gates.

> Implement and verify the work, keep the handoff current, and distinguish automated proofs from real-machine acceptance. Preserve Nix/Home Manager delivery and rollback. Privileged steps and exact Test, Run and Release candidates retain their separate approvals.

## Implemented

- Bounded stable regular-file reads for settings metadata, exact required
  fields, bounded numeric values and state/presence semantics. Capture base
  identities and completeness agree with their file records. Archive size
  checks occur before extracting capture metadata and snapshots. FIFOs do
  not block reads; descriptor-relative traversal refuses links and fails
  closed if `openat2` is unavailable.
- System/user agreement includes the selected uid, target, exact system
  transaction id and, for product changes, source and shared transaction id.
  Completed and missing user records cannot bypass that check.
- The transaction's new `changes` hash binds exact `CHANGES` bytes before
  applying. Retry refuses truncated or replaced plans. Its new `activation`
  hash binds ordered user action names, requirement flags and helper paths.
  A changed plan restarts idempotent actions at zero before retrying.
- Absence records now use mode `0000` and reason `-`; removal records use
  new mode `0000`. Fault enumeration caught and verified both writer/reader
  inconsistencies during the audit.
- Private import/export uses the user state lock and private staging under
  that user's state. Exclusive random temporary files avoid truncating
  pre-existing metadata temporaries. Export syncs a complete mode-0600
  archive before atomic, non-replacing publication, then syncs its parent.
  Import is repeatable after capture publication.
- The public design and manual describe the concrete formats and retry
  behavior. They remain unaccepted pending the developer's review.

## Verification

`make test` passed on 2026-11-10 with an exclusive checkout claim. The
format-2 product and settings binaries use ASan/UBSan; no finding occurred.

- **25 product tests:** all green, including 57 prepare and 29 apply fault
  events, 12 randomized kills and recovery/ownership-of-path fixtures.
- **21 settings tests:** all green, including 19 capture, 4 upgrade-prepare,
  4 restore-prepare, 63 apply, 64 export and 15 import fault events. Every
  enumerated interruption was retried or its completed output verified.
  An existing export output was preserved byte for byte on refusal.
- **36 settings randomized kill trials:** 12 apply and 6 each for capture,
  restore preparation, export and import. These are sampled interruptions;
  no claim is made that every random kill landed during mutation.
- Deterministic atomic replacement during each of three read attempts
  produced `changed-during-read` exclusion. Oversized metadata, FIFO input,
  malformed/overflowed records, hostile archives, corrupt retry plans,
  activation-plan replacement and stale/disagreeing transactions refused
  without changing the affected user's settings.
- Namespace containment, format-1 compatibility and its 11 named faults
  and 30 random kills, build/lock/cache, file selector, manual formatting,
  help coverage and polkit XML checks all passed. Signal tests require a
  private PID namespace and newly mounted private `/proc`; no host-process
  fallback exists.

These were temporary-root tests on tmpfs. They do not prove power-loss
safety, real session/login integration, settings layering or Home Manager
link migration, second-workstation restoration, console repair or human
acceptance. The installed 0.5.2 binary and assembly pin were untouched.
Nix/Home Manager delivery and rollback remain in use. No privileged
installation, candidate stage move, release or Nix retirement occurred.

## Continuation

Review the concrete formats at the start of
[`docs/plans/tam-bundle.md`](../plans/tam-bundle.md#07-development-checkpoint).
The developer confirmed readiness and the exact design file was displayed
in Micro. Format acceptance is pending; it precedes
0.8, then recipes/tam nut, candidate integration and real-machine migration
proofs. Each privileged step and exact Test, Run and Release still needs
its separate approval.
