# tam-bundle 0.6 completion and 0.7 settings draft

- **Date**: 2026-11-10
- **CLI Tool**: Codex CLI (`codex`) `0.167.0`
- **Model**: `gpt-6.1-sol`
- **Transcript**: Retained privately by the author.
- **Authorship**: AI-assisted implementation and documentation, continuing
  the recovered draft at `b4f1c15`. The developer reviews the concrete
  formats separately from deployment and stage authorization.

## Guiding prompt (excerpts)

> First finish 0.6 against its accepted scope, including documenting the implemented transaction format, archive layout and fault points, and presenting the required design changes for my review.

> Then continue through the remaining migration: 0.7 settings capture/restoration; 0.8 ownership and runtime checks; recipes and tam-nut; tam-deploy and candidate-record integration; and second-workstation proofs before bosgame cutover.

> Implement and verify the work, rather than stopping at another proposal. Keep the migration plan and continuation handoff current. Distinguish automated test results from real-machine acceptance.

> Follow the existing review gates and privileged-step approvals. Test, Run and Release still require my separate words for exact candidates. Preserve Nix/Home Manager delivery and rollback until the migration proofs and recovery requirements are satisfied.

Review response:

> Approve 0.6 formats; continue 0.7

Checkpoint question:

> Is this a good place to end this session?

## Changes

- Apply now renames the files prepare already wrote and synced. Retained
  version copies stay intact; retry recognizes already replaced files.
- Recovery checks all destinations before further mutation and refuses
  foreign bytes, links or a current identity outside the transaction. The
  activating phase checks the completed file set before retrying actions.
- Transaction records require every field exactly once, reject signed
  numbers and unsupported formats, and validate the random transaction id.
- Newly created parent directories are synced. Private directory modes are
  preserved and checked. Empty payload roots remain required and installable.
- Tree links cannot escape into the archive's other section; installed
  metadata and `.placed` names are reserved. Archive verification is
  available through `verify`. Status reports the previous product and the
  full current bundle hash.
- The public design now defines the actual transaction record, archive
  order, installed layout, temporary rootstock profile and fault points.
  Pinned recipes belong under `tree/recipe/`, covered by the manifest.
- Signal-test containment remains mandatory: private PID/user/mount
  namespaces, a freshly mounted private `/proc`, and no host fallback.

## Verification

`make test` passed on 2026-11-10 from the build tree, with an exclusive
checkout claim. The format-2 binary and busy-check fixture use AddressSanitizer
and UndefinedBehaviorSanitizer. No sanitizer finding was reported.

- All **25 format-2 tests** passed, including enumeration and recovery at
  **57 prepare** and **29 apply** events and **12 randomized SIGKILL** trials.
- Cancel and completion archival faults, hundredth-file interruption,
  malformed records, edited recovery destinations, staged corruption,
  nonadjacent archive restoration, required-action retry and restart of old
  services on decline/recheck failure passed.
- Namespace containment proved an outside sentinel neither visible through
  private `/proc` nor addressable by pidfd, while the sentinel stayed alive.
- Format-1 tests passed, including all 11 named faults and 30 randomized
  kill trials. Existing build/lock/cache, file-selector, manual formatting,
  help coverage and polkit XML checks passed.

These tests used temporary roots on tmpfs. They do not establish power-loss
safety, real login/boot integration, second-workstation restoration or human
acceptance. The installed 0.5.2 executable, assembly pin and workstation
delivery were untouched. Nix/Home Manager delivery and rollback remain.

## Subsequent 0.7 development checkpoint

After the developer approved 0.6 and directed continuation, this session
implemented the settings draft in `src/tam-bundle/settings.c` and its CLI,
archive/identity support, manual and tests. It reports 0.7.0 in the build
tree. It has not been installed, reviewed as complete or accepted.

The draft includes private captures, presence, secret exclusions, stable-read
checks, host marks/identity, automatic switch-time capture, three-way
defaults and consumed `.tamlinux-new` cleanup, user file transactions and
required activation retries, private workstation export/import, derived
product identity for saved edits, settings diff and an agreement check.
System archive preparation now keeps the trusted input in a private
temporary directory so personal captures never enter public product state.

The final `make test` passed on 2026-11-10 with ASan/UBSan, mandatory PID
namespace/private-proc containment and no host fallback:

- All 25 format-2 product tests remained green (57 prepare events, 29 apply
  events and 12 randomized kill trials).
- All 13 settings tests passed, including 59 user apply events and 12
  randomized kill trials, activation failure/retry, refusing foreign edits
  and stale transactions, secret and host handling, three-way behavior,
  settings diff and `.tamlinux-new` cleanup.
- A transferred archive restored settings after the original temporary home
  was removed. A fresh temporary root received only product files in its
  system state; its separate private home imported and restored the capture.
- Edited product export refused by default; `--save-edits` produced a
  verified derived identity and carried the actual edited executable.
- Format-1/build/cache tests, manual/help and polkit checks remained green.

This is a clean development checkpoint. Before 0.7 acceptance, finish the
parser/metadata and transaction audit, including activation-profile changes
during retry; extend durable coverage to capture, user prepare and
import/export; then review the concrete draft formats with the developer.
The public design records these open items. Actual settings layering and
Home Manager link migration, native ownership/runtime checks, the tam nut,
login and candidate integration, real hardware proofs and cutover remain
later work. No claim of real-machine acceptance follows from these tests.
