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

Review response:

> Approve 0.7 formats; continue 0.8

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
  behavior. The developer approved the 0.7 formats after reviewing them.

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
in Micro, then approved its 0.7 formats and directed continuation through
0.8, then recipes/tam nut, candidate integration and real-machine migration
proofs. Each privileged step and exact Test, Run and Release still needs
its separate approval.

## 0.8 tool checkpoint

After 0.7 acceptance, this session implemented 0.8 native ownership and
runtime contracts, build environment identity, checked caches, offline
builds and a fully static production tool. The public design's 0.8
checkpoint specifies the actual rootstock profile id, requirement forms,
ELF and QML checks, `recipe/BUILDENV`, and cache `done` record.

The developer reviewed the exact checkpoint in Micro and responded:

> Approve 0.8 tool; continue recipes

- Arch package queries cover database-listed paths even when absent on
  disk. Prepare and apply/retry refuse native conflicts and unmet runtime
  requirements. Target checks run first; `status` and `check` report
  compatibility of all retained versions. The rootstock database is a
  trusted input; no concurrent hostile root writer is modeled.
- The ELF inspector reads headers, interpreter and dynamic dependency
  records with bounded reads; it never executes inspected programs.
  Build output checks shebangs and ELF files for undeclared dependencies.
  QML version proof requires a declared module/version or literal export.
  A real Python import probe requires the running rootstock.
- Build identity includes the exact selected native package versions;
  they are rechecked before recording a finished cache entry and publishing
  the bundle. Native build package changes invalidate the applicable cache;
  unrelated packages do not. Source resolution/fetching precedes make.
  Builds use `/usr/bin` and a new network namespace. Cache hits check their
  output manifest and tree, with full hashing under `--verify`.

The final `make test` passed on 2026-11-10 with the exclusive checkout
claim. It reran the 25 product and 21 settings tests and their existing
fault/kill proofs. All 10 new runtime tests passed, including native
database/ownership refusals, retained compatibility, minimum and exact
versions, QML/Python fixtures, missing and malformed ELF dependencies,
selected product interpreters, and the production binary's lack of
`PT_INTERP` and `PT_DYNAMIC`. Two new build proofs passed: selected-package
invalidation with a synthetic database in a private mount namespace, and
seven cache/publication fault boundaries plus 12 randomized kills. The
existing build/lock/cache, format-1, file selector, manual, help, polkit and
outside-sentinel containment checks passed. ASan/UBSan reported no finding.

The proof is for the tool in temporary roots and private namespaces, with
no host-process fallback. It is not an accepted native workstation
installation or a power-cut, login, console or Nix recovery proof. The
existing builder still builds its legacy slot layout; the format-2
workstation recipe split, tam nut and delivery integration are next. No
assembly pin, Nix/Home Manager generation, candidate or release moved.

## Format-2 workstation recipe checkpoint

After reviewing the 0.8 tool checkpoint in Micro, the developer said
“Approve 0.8 tool; continue recipes”.

The development builder reports 0.9.0. It builds format-2 recipes with
`PREFIX=/usr`, checks their placed paths and dependencies, and carries
the exact recipe, lock and build environment in the versioned tree. The
Quickshell Wayland probe reads the module's declared `typeinfo` filename
and tests the exported version. Tar metadata is normalized so two builds
at the same recorded time can produce the same archive bytes.

The [workstation recipe proof](../plans/tam-bundle.md#format-2-workstation-recipe-checkpoint)
names the exact packaging commit, output identity and archive SHA-256.
A verified-cache build and a clean-cache build sealed the same manifest
and metadata. Both archives compared byte for byte and the archive
verified. The fixture also exercised format-2 apply and recovery in
temporary roots. The tar comparison caught differences in timestamps and
directory order before normalization.

The development artifact has no Test, Run or Release authorization.
The developer reviewed the recipe proof in Micro and said “Approve recipes;
continue tam nut”. The tam nut, delivery integration and second-workstation
proofs remain.
