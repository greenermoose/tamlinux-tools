# tam-bundle — design

- **Status:** Revised 2026-11-10 for the developer's requirements of that
  day and reviewed by him the same day. The review settled the tam nut,
  capturing nothing from `/etc`, and keeping the init system out of
  tam-bundle's core; the remaining questions are under "Open". The earlier design (approved 2026-11-09)
  installed everything into one slot, `/usr/lib/tamlinux/current/`, put
  `current/bin` on `PATH`, swapped whole trees with
  `renameat2(RENAME_EXCHANGE)` and left personal configuration out. The
  code implementing it (`tam-bundle` 0.1.0 to 0.5.2) passed real install,
  switch, rollback and forward switch in a dormant slot; that remains
  evidence for the parts this design keeps. What changes, what stays and
  what goes is listed in "From the earlier design". Git keeps the earlier
  text.
- **Component:** `tamlinux-tools`, its own version number, C on `libtam`,
  linked statically.
- **Survey:** [2026-11-09 — File bundles, install records and atomic
  switching](../../upstream/2026-11-09-file-bundles-and-atomic-switch.md).

## 0.8 implementation checkpoint

The build-tree tool reports 0.8.0; it is not installed. Native ownership,
runtime contracts/probes, offline builds, recorded build environments,
checked caches and the fully static production executable are implemented.
The earlier 0.6 and 0.7 formats remain approved. This checkpoint does not
approve a candidate, workstation installation or Nix retirement.

### Concrete interfaces

- Native ownership uses bounded, read-only Arch `pacman -Q` and `-Ql`
  queries with an explicit root/database and no caller configuration.
  Database-listed placed files conflict even when absent on disk. Both new
  and replaced Tamlinux paths are checked. Missing, changing or locked
  databases refuse; `/usr/local` and unrelated packages remain untouched.
- `needs` honors `--root` for format 2. Architecture/CPU baseline, glibc
  loader and `target profile` checks precede dependency checks. The trusted
  profile identity is a single line in
  `/usr/share/tamlinux/rootstock/id`, such as `arch-systemd`.
- `needs file /path [>= version|exact sha256:hash] [arch=package ...]`
  checks native ownership/version or exact bytes. Present files with no
  native owner report **version unknown**, as decided. `needs tam-bundle
  >= version` checks the tool version. Legacy `command name` remains an
  alias for `/usr/bin/name` in format 2; format 1 keeps its original PATH
  presence check and simple requirement syntax.
- `needs qml Module.Name major.minor` reads the standard Qt 6 `qmldir`
  and literal module exports in `plugins.qmltypes`. Comments, unrelated
  numbers and unversioned exports cannot establish a requested minimum.
  `needs python module.name` uses the rootstock's `python3 -I` import
  probe. Python imports require the running rootstock; an offline foreign
  root is refused instead of borrowing host imports. Temporary-root tests
  use explicit isolated fixture executables.
- `needs elf usr/bin/program` inspects ELF64 little-endian x86_64 files,
  their interpreter and recursive dependencies without executing them.
  The build scans all payload ELF files and shebangs, including `env`
  interpreters, and refuses undeclared external dependencies. Product
  interpreters/private libraries need no external requirement. Rootstock
  defaults and literal absolute or `$ORIGIN` search paths are supported;
  unsupported expansion/search assumptions refuse. This is the first
  rootstock's implementation, not a portable ELF loader.
- Prepare and every apply/retry check runtime compatibility and native
  ownership before file mutation. `status`/`check` also report every
  retained product's runtime compatibility, so a broken retained recovery
  version is visible before it is needed. Login integration and native
  package-change hooks remain tam-nut work.

The native query interface follows the
[pacman query documentation](https://man.archlinux.org/man/pacman.8.en).
ELF inspection follows the file's `PT_INTERP`, `PT_DYNAMIC` and `DT_NEEDED`
records described in the [ELF ABI](https://refspecs.linuxfoundation.org/elf/gabi4%2B/ch5.dynamic.html).

### Build environment and cache records

Each component recipe accepts `build-needs package [>= version]`, up to
64 declared native packages. The fixed toolchain base is gcc, binutils,
glibc, linux-api-headers, make and pkgconf. The native versions of just
these selected packages enter that component's input key; unrelated
packages do not. The selected versions are rechecked before recording a
finished cache entry and before bundle publication.

The combined record is shipped as `recipe/BUILDENV` by the existing
format-1 builder; the format-2 recipes will put it under `tree/recipe/`:

```text
format 1
component app
package binutils 2.46-1
package gcc 16.2.1-1
package glibc 2.43-1
package linux-api-headers 7.0-1
package make 4.4.1-2
package pkgconf 2.5.1-1
```

Package rows are sorted and deduplicated per component. `BUNDLE` gains
`buildenv sha256:<64 hex>`, covering the exact combined record. A derived
product export preserves its build-environment identity.

Each component cache directory retains `out.MANIFEST` and `out.stats`.
Its last-written `done` stamp is:

```text
format 1
key <64 hex input key>
hash sha256:<64 hex of out.MANIFEST bytes>
```

Cache hits check the stamp, manifest hash and actual output tree. Ordinary
checks reuse unchanged file stats; `build --verify` fully hashes files.
Invalid, missing or edited cache records/outputs rebuild. The caller's
PATH is excluded: tool paths are absolute, build PATH is `/usr/bin`, PATH
and SHELL recipe overrides are refused, and every make runs in a private
network namespace. All source fetching precedes the first make. The
production tool has neither an ELF interpreter nor a dynamic section;
sanitizer test binaries remain separate and are never installed.

### Verification boundary

Temporary fixtures verify native ownership, target/version/probe refusal,
retained compatibility, ELF non-execution and static linking. The build
proofs cover selected versus unrelated native versions, corrupt caches,
fixed PATH/network namespaces, seven cache/output publication boundaries
and randomized kills. Product and settings recovery suites retain their
mandatory private PID namespace and private `/proc`, without a host
fallback. `make test` passed all 25 product, 21 settings, 10 runtime and
two new build proof tests, plus the existing compatibility and manual
checks. The [session record](../ai/2026-11-10-tam-bundle-07-audit.md#08-tool-checkpoint)
gives the exact run results.

These are automated tool proofs. Native package updates are trusted
rootstock operations, not an adversarial root writer. This is not a
filesystem power-cut proof, real QML/Python/UX acceptance, an installed
second workstation, or a demonstrated login/console/Nix recovery path.
The format-2 build recipes, real settings list and Home Manager link
migration, tam nut, delivery/candidate integration and hardware proofs
remain the next migration steps. Nix/Home Manager delivery and rollback
and the bootstrap pin remain unchanged.

## 0.7 development checkpoint

**Approved by the developer, 2026-11-10:** "Approve 0.7 formats; continue 0.8".
The implementation and audit are complete. Development continues through
0.8 ownership/runtime/build checks. The build-tree tool reports 0.8.0, while the
installed tool and assembly pin remain at 0.5.2. Nothing here authorizes
installation or a stage move.

The draft implements private captures, explicit presence, secret exclusion,
host marks, automatic capture at apply time, three-way defaults and
`.tamlinux-new` cleanup, private workstation export/import and derived
product exports. The settings half has its own recoverable file transaction
and required user activation steps. The CLI exposes these parts separately
so temporary-root tests can prove them before the delivery front end is
wired. They are not yet the workstation's login or delivery mechanism.

### Approved formats and interfaces

Product defaults used by the settings engine live at
`tree/defaults/home/<home-relative path>` in an archive, or
`version/<id>/defaults/home/<path>` installed. The product's `SETTINGS` and
the user's `settings.list` select files; a trailing slash expands the files
under that directory, including defaults for files absent in home.
User marks override product marks. Only explicit `absent` records delete
files. Unlisted files and excluded files are left alone.

```text
CAPTURE:
format 1
user 1000
host workstation
host-id sha256:<64 hex>
product tamlinux-0.7.0-<12 hex>
created 20261110T180000Z.123456789
complete yes
hash sha256:<64 hex>

FILES:
format 1
common present 0644 sha256:<64 hex> edited <base product id> - 0 .bashrc
common absent 0000 - - <base product id> - 0 .config/old.conf
common excluded 0644 - - <base product id> secret-pattern 2 .config/private.conf
```

`FILES` fields are mark, presence, original mode, hash, origin, base,
exclusion reason, line number, path. The last field is the rest of the
line, so spaces within a path need no quoting; leading/trailing spaces,
tabs, controls and unsafe relative paths are refused. The capture hash covers the exact `CAPTURE` bytes without its
hash line, followed by `FILES`; file hashes cover the private snapshots.
The id is `<numeric user>@<host>-<date>-<hash 12>`; the nanosecond timestamp
in the hash distinguishes captures made within one second. Capture files
are `0600`, directories `0700`. Files larger than 16 MiB and collections
over 16,384 paths are not silently accepted. `CAPTURE` and transaction
records are limited to 4 KiB each; `FILES`, `CHANGES`, settings lists and
the activation profile to 16 MiB each. Paths are shorter than 4,096 bytes;
directory expansion stops beyond 64 separators. `CHANGES` allows up to
32,768 operations (a setting and its side file); user activation allows
128 distinct steps. Metadata must be a stable regular file. The archive
reader enforces capture metadata and snapshot size limits before extraction.
Reads compare inode, size and
change time, retrying changed files three times. Links are currently
excluded, including Home Manager links; converting managed settings to
real files remains part of the migration and its recovery proof.

The system's `host-id` is a public SHA-256 string. Prepare initializes it
from a fixed prefix plus a valid DMI UUID; missing/placeholder UUIDs use a
persistent random fallback and print a note. The raw UUID is never exported.

The user transaction uses its own `format 1` record:

```text
id <32 random hex, or the system transaction id>
system <exact system transaction id>
from <old product id>
to <new product id>
capture <selected retained capture id, or ->
automatic <switch-time capture id, or ->
kind upgrade
state ready
done 0
activation -
changes -
```

All fields are required and unique. Ordinary upgrades/restores share the
system transaction id. A settings-only switch has its own id and binds the
exact completed system transaction through `system`; a stale record cannot
pass after a product changes away and back. This settings-only rule is a
draft refinement of the final design's two-record agreement, for review.
Both readers reject missing, duplicate and unknown fields, unsupported
formats, overflow and impossible presence/state combinations. The selected
user must match the system record's uid. Product restores require the same
transaction id, source and target in both records; settings-only restores
bind the completed system id and target. Both `apply-settings` and
`session-ready` reject stale or disagreeing records, even if the user
record already says complete. `session-ready` is a file-level check, not
installed session integration.

Prepare writes only a plan. At apply, the engine takes the automatic
capture, computes the result again and writes a private staged `CHANGES`:

```text
format 1
write 0644 0644 <old hash or -> <new hash> .bashrc
remove 0000 0644 <old hash> - .config/old.conf
```

Fields are operation, new mode, old mode, old hash, new hash, path. The
engine checks every destination and staged source before any home rename.
Before entering **applying**, `changes` becomes the SHA-256 of the exact
`CHANGES` bytes. Every retry verifies it before using the plan. An empty,
truncated or changed plan cannot discard pending operations. It remains
`-` during **ready** and **staging**. Removal records use new mode `0000`;
an absent old file uses old mode `0000`.
Both old and new contents/modes are recognized during retry; a foreign
edit leaves the transaction incomplete. It checks mount ids, refuses links
in parent paths and never writes outside the selected home. Required
activation failure leaves **activating**, with completed steps counted.
The trusted rootstock profile gains
`user-step user-units-reload required <relative helper path>`; that required
step must be present before the user record can become **complete**.
`activation` is `-` until the activation phase loads its trusted profile,
then the SHA-256 of the ordered step names, required/optional flags and
helper paths, serialized as `format 1` with one three-field record per
step. The hash and `done 0` are recorded before actions run. If that plan
changes during retry, actions restart at zero; an old count cannot skip a
new required action. Helpers must be idempotent and support repetition.
Failure remains **activating**; completion requires a valid plan hash.

New commands are `capture [-o FILE] [--save-edits]`, `import-settings FILE`,
`prepare-settings [CAPTURE]`, `restore-settings CAPTURE`, `apply-settings`,
`session-ready` and `settings diff PATH`. Workstation archives add a private
`capture/` to the format-2 product archive. Export verifies running product
bytes; edits require a new bundle hash, `derived-from` and explicit `edit`
lines. Import verifies every listed snapshot and refuses unrecorded capture
files. Workstation archives are `0600` and refused under public product
state. Export and import hold the user's state lock. Export writes a
mode-0600 sibling temporary file, syncs its complete bytes, publishes it
atomically with a non-replacing hard link, removes the temporary name and
syncs the parent. An existing output is preserved and reported as a
failure; after interruption, verify any published output before reusing it.
Incomplete exports never appear at the requested output name. Import
publishes a verified private capture by rename and is repeatable after
publication. Abandoned private staging directories are never treated as
retained captures or active plans. System prepare uses a private temporary archive copy and copies only
the product manifest's payload into system staging.

### Verification and remaining acceptance

The completed audit covers bounded stable metadata reads, semantic capture
records, exact transaction agreement, immutable retry plans and activation
profile changes. The settings suite has 21 tests under ASan/UBSan. It
enumerates 19 capture, 4 upgrade-prepare, 4 restore-prepare, 63 apply, 64
export and 15 import fault events, and runs 36 randomized kill trials
across those operations. Deterministic atomic replacement during all three
read attempts produces an exclusion; hostile archives, FIFOs, oversized
metadata, invalid fields and corrupt retry plans are refused. Signal tests
require private PID namespaces and a freshly mounted private `/proc`;
there is no host-process fallback.

- The developer reviewed and approved these concrete formats before
  advancing to 0.8. The actual settings list and layering recipes,
  login/tam nut wiring and candidate delivery integration
  remain later work. Real workstation, power-cut, console-repair and
  Nix/Home Manager rollback proofs remain mandatory.

## 0.6 implementation review

**Approved by the developer, 2026-11-10:** "Approve 0.6 formats; continue 0.7".
These are the concrete formats and recovery choices implemented by 0.6.0.
This closes the implementation-format review; it does not authorize
installation or a Test, Run or Release. The later sections describe the complete design;
settings, native ownership/runtime checks and delivery integration are
still later migration steps.

### Archive and installed layout

`seal --format 2` reads `tree/` and `placed/`, both real directories with
mode `0755`. `SETTINGS` is a separate identity input, created as `format 1`
with no entries when absent. It is carried unchanged, without interpreting
or applying settings in 0.6. `target` lines are repeatable descriptions,
included in the hash; their runtime checks belong to 0.8. The default seal
and the existing recipe builder still produce format 1 until recipe work.

`pack DIR -o FILE` writes an uncompressed POSIX pax tar in this order:

```text
BUNDLE
MANIFEST
SETTINGS
placed/...
tree/...
```

The reader accepts only these records and payload roots. Each directory
precedes its children; `BUNDLE` and `MANIFEST` must be the first two payload
members. Per-file pax headers may precede them and supply `path`,
`linkpath`, `mtime`, `atime` or `ctime`; other pax keys are refused. Pax
data is limited to 64 KiB and one file to 16 GiB. Headers have checked
octal sizes, checksums and modes at most `0777`. Two zero blocks terminate
the archive. Duplicate members, absolute paths, `..`, hard links, device
nodes and placed symlinks are refused. Extraction opens parents without
following links; full verification checks the extracted manifest and hash.
`verify` accepts a directory or an archive. Archive verification uses a
private temporary directory and removes it afterward.

The format-2 manifest is sorted by the expanded archive path: all `p`
entries (`placed/...`) first, then `t` entries (`tree/...`), parents before
children. The structural `placed/` and `tree/` entries are implicit;
their children are explicit. Every other directory has its own manifest
entry. A tree link must stay within `tree/`, not merely within the archive.
`BUNDLE`, `MANIFEST`, `SETTINGS` and `.placed` are reserved at the top of
`tree/`, because they have installed meanings. Recipe material goes under
`tree/recipe/`, covered by the tree manifest, rather than in an unrecorded
top-level `recipe/` directory. Adding the actual pinned recipes is a later
migration step.

Prepare copies and verifies the bundle under
`/usr/lib/tamlinux/staging/<transaction id>/`, with its archive layout.
It also materializes and syncs the retained version:

```text
/usr/lib/tamlinux/version/<bundle id>/
    BUNDLE, MANIFEST, SETTINGS
    <contents of tree/>
    .placed/<contents of placed/>
```

That version is published by renaming `staging/<transaction id>/version`
into `version/<bundle id>` and syncing the destination directory, before
the record becomes **ready**. Apply renames prepared files from
`staging/<transaction id>/placed/...` over their standard destinations;
it does not copy replacement bytes. The retained `.placed/` copies remain
available for rollback. `current` switches with one rename of a temporary
relative symlink. A retained version is verified before it is reused.

### Transaction record

The transaction has its own record format, independent of bundle format 2:

```text
format 1
id 0123456789abcdef0123456789abcdef
from tamlinux-0.6.0-0123456789ab
to tamlinux-0.6.1-abcdef012345
capture -
uid 1000
kind product-only
state ready
done 0
```

All eight keys are required exactly once; unknown keys, duplicate keys,
negative numbers and newer record formats are refused. `id` is 16 random
bytes rendered as 32 lowercase hex digits. `from` may be `-` for an empty
installation; `to` must name a different bundle. `capture -` and
`kind product-only` are the only values in 0.6. `uid` records the invoking
numeric user; the settings/privilege integration will bind the initiating
user in later versions. `done` counts the completed rootstock profile
steps, so a failed required step is retried while **activating**.

The five states and their recovery meanings are the table under "The
transaction". Each write uses `.transaction.new`, file `fsync`, rename,
and parent-directory `fsync`. The same sequence writes `record` (the new
format-2 manifest) and `keep` (`format 1`, then `previous <old id>`).
A completed transaction stays visible until the next apply or prepare
renames it to `last-transaction` and syncs the state directory. In 0.6 this
is the last completion record, not an accumulating history. Cancel first
unlinks and syncs the **staging** or **ready** record, then removes scratch;
an interruption may leave unused staging, never an armed missing source.

Before entering **applying**, prepare's file/mount checks are repeated and
damaged rollback copies are repaired from verified live old files. Native
package ownership and runtime checks are deferred to 0.8. During recovery,
every destination must match a state this transaction could have made:
old bytes, new bytes, or an allowed absence. An unknown file, symlink or
`current` identity stops the entire retry before further live changes.
Before activation steps, all new files must be in place. A required step's
failure keeps **activating** and returns exit 3; incomplete or malformed
transactions return exit 4. A busy decline leaves **ready**, restarts the
old services and returns success so a future launcher can use the old
version. The session barrier itself is later delivery integration.

`status --root ROOT` shows the current and previous bundle ids, full
current bundle hash, and pending transaction id, state, from/to and capture.
The input key remains the builder's cache identity; it is not the bundle
hash or the random transaction id. Capture identity is `-` until 0.7.
`restore BUNDLE` prepares any retained directory or transferred archive;
it does not require the immediately previous version.

### Temporary rootstock profile

The tests supply a profile at
`usr/share/tamlinux/rootstock/profile` beneath their temporary root:

```text
format 1
allow usr/bin
allow usr/share/man
services usr/share/tamlinux/rootstock/services
step system-daemon-reload required usr/share/tamlinux/rootstock/reload
step man-index optional usr/share/tamlinux/rootstock/man-index
```

This is trusted local input, a regular file owned by the effective user
and not group- or other-writable. Paths are relative to the installation
root. The profile cannot allow `/etc`, `/usr/local`, the product's private
tree, home paths or non-`tam`/`tam-*` commands; `tam-bundle` and `tam-nut`
themselves are reserved for the native package. Commands are executed
directly, with a 30-second deadline: the service helper receives `stop`,
`restart-old` or `restart-new`, and a step receives `apply`; each also
receives the transaction id. Helpers must be safe to repeat after a crash.
The profile and real boot/login approval, ownership and trigger wiring
remain tam nut work. This test interface does not authorize a live install.

### Fault points and evidence boundary

Only the sanitizer test executable honors these controls:
`TAM_BUNDLE_TRACE=FILE` records a numbered event stream;
`TAM_BUNDLE_FAULT_AT=N` exits 99 at one event;
`TAM_BUNDLE_FAULT=NAME` exits at the first matching name;
`TAM_BUNDLE_SLOW_US=N` delays file iterations for randomized SIGKILL tests.

| Fault name | Boundary just reached |
| --- | --- |
| `v2-directory-sync` | directory `fsync`, including newly created parents |
| `v2-filesystem-sync` | filesystem `syncfs` |
| `v2-copy-sync` | prepared or rollback-copy file `fsync` |
| `v2-record-file-sync` | temporary record file `fsync` |
| `v2-record-rename` | atomic record replacement, before directory sync |
| `v2-state-staging`, `-ready`, `-applying`, `-activating`, `-complete` | the named state and its directory sync |
| `v2-version-rename` | retained version rename, before directory sync |
| `v2-placed-rename` | prepared placed-file rename, before both directory syncs |
| `v2-obsolete-rename` | obsolete placed file moved to old `.placed/` |
| `v2-current-rename` | current-link rename, before directory sync |
| `v2-complete-rename` | transaction archived, before directory sync |
| `v2-cancel-unlink` | cancelled record removed and directory synced |
| `stage-100`, `stage-200`, ... | each hundredth file iteration |

The tests enumerate every reached prepare/apply event, interrupt it, and
retry, and separately exercise cancel, completion archival and hundredth
file faults. Process selection tests always require a private PID
namespace and a freshly mounted private `/proc`; namespace setup failure
fails the suite, with no host-process fallback. These are automated
temporary-root results. They do not prove real session gating, power-loss
recovery, second-workstation recreation or human acceptance. Those remain
mandatory before bosgame cutover; Nix/Home Manager delivery and rollback
remain in use.

## What tam-bundle is for

One bundle describes and carries an exact Tamlinux installation: how it was
built, the files it installs, the settings that make up the workstation
experience, the third-party software it expects to find, and how its parts
are activated together. With it the developer can:

- build a version of Tamlinux from exact commits;
- capture a working workstation, settings included, into a bundle;
- carry that bundle to another Tamlinux workstation and install it there;
- roll forward and backward to any retained bundle;
- never be left with a half-installed system in use.

Tamlinux is system software, so it installs the way system software has on
Linux for decades: commands in `/usr/bin`, private files (ones only
Tamlinux programs use) under `/usr/lib/tamlinux`, shared data in its usual
`/usr/share` places, and nothing in `/usr/local`, which belongs to the
machine's administrator.

It replaces Nix and Home Manager for Tamlinux, without their store, symlink
chains, daemon or language.

## Requirements

1. **Standard places.** Every command Tamlinux installs is the actual
   executable in `/usr/bin`: never a link, never anywhere else. Files that
   other programs find by a standard path (manual pages, desktop entries,
   systemd units, polkit actions, fontconfig and environment drop-ins) are
   real files in those places. Everything else, the bulk of the product,
   lives under `/usr/lib/tamlinux`.
2. **Links switch versions; nothing is found through them by hand.** The
   product tree sits in `/usr/lib/tamlinux/version/<id>/`, reached through
   one link, `/usr/lib/tamlinux/current -> version/<id>`. No other links
   are needed to find a Tamlinux file.
3. **A plain PATH.** Tamlinux adds no directory of its own to `PATH`.
   `PATH` is `~/.local/bin`, `/usr/local/bin`, `/usr/bin`, plus whatever the
   rootstock adds itself.
4. **One bundle, one identity.** A product bundle has a name, a version,
   the exact source commits, its recipe and one content hash. A settings
   capture has its own hash, so two captures of one version differ.
5. **Never half-baked.** A new version is prepared while the user works and
   switched in the next time the desktop starts, before the graphical
   session runs. An interrupted switch is finished before any session
   starts. No reboot.
6. **Dependencies are listed, not carried.** A bundle contains the Tamlinux
   software, a list of the third-party files it expects and where, and a
   recipe for installing the Tamlinux software. It never carries Hyprland,
   Quickshell or their libraries. The rootstock only has to let tam-bundle
   run; tam-bundle checks the list, names what is missing, and the user
   resolves it. A bundle is as portable as that check. The promise is
   exactly this: tam-bundle restores these exact Tamlinux files and
   selected settings **when the current rootstock satisfies their runtime
   contract**; when it does not, tam-bundle says so before switching.
7. **Settings are part of the bundle.** The settings that make up the
   Tamlinux experience are captured and restored with the product. Big
   application state is left out. Known secret folders are always refused
   and other files are scanned for secrets, a safeguard rather than a
   guarantee, so captures are always private.
8. **Hosts.** A bundle knows the host it was made on. On that host
   everything applies. On another host, host-specific settings are left out
   and the common ones apply, so the user's own settings follow them.
9. **Three owners.** Every file belongs to the rootstock (its own package
   manager), to Tamlinux (tam-bundle's record) or to neither. tam-bundle
   writes only its own files, refuses before changing anything if a path
   it would write belongs to someone else, and leaves everything else
   alone, wherever it is.
10. **Nothing in the background.** No daemon, socket, timer or polling. It
    runs when invoked and exits. No network unless asked.
11. **No init system in the core.** The bundle format, prepare, apply,
    recovery and captures work the same under systemd and runit. What
    depends on the init system (the boot trigger, the folders files may be
    placed in, the post-install steps) comes from the tam nut's rootstock
    profile, so a runit rootstock needs its own tam nut, not its own
    tam-bundle.
12. **Fast and cheap.** Nothing to do costs a stat or two when the desktop
    starts. Preparing a version with nothing to rebuild takes seconds. Disk
    is spent to save CPU and time.
13. **Ours.** C on `libtam`, statically linked, with a man page, `tam`
    pages and its own tests, including the crash and older-bundle tests.

**Out of scope:** a general package manager; building or shipping
third-party software; binary caches and remote servers; backups (restic
keeps those, including everything a capture leaves out).

## Words used here

- **Rootstock:** the base distribution (Arch today, Void later) with its
  own package manager.
- **Product bundle:** a built version of the Tamlinux software, ready to
  install: its identity, file list, dependency list and files.
- **Settings capture:** one user's Tamlinux settings at one moment, with
  their host marks. Only files in the home directory; nothing from `/etc`.
- **Tam nut:** the smallest piece of Tamlinux, one native package installed
  on a rootstock, holding `tam-nut` and `tam-bundle` (see "Installing
  Tamlinux").
- **Rootstock profile:** the part of the tam nut that says how this
  rootstock works: which package manager answers ownership questions, which
  folders files may be placed in, which of them affect login and boot,
  which post-install steps exist, and how to stop and restart a version's
  user services.
- **Workstation bundle:** a product bundle plus a settings capture; what
  "bundle this up" produces and what is carried to another workstation.
- **Product tree:** the part of a product bundle installed under
  `/usr/lib/tamlinux/version/<id>/`.
- **Placed files:** the part installed at standard paths outside the
  product tree (`/usr/bin/tam`, `/usr/share/man/man1/tam.1`, ...).
- **Bundle id:** `<name>-<version>-<first 12 hex digits of the hash>`,
  for example `tamlinux-0.7.0-3f9a0c1b2d4e`. A capture id is
  `<user>@<host>-<date>-<12 hex>`.
- **Prepare** stages and verifies a bundle while the user works.
  **Apply** switches it in when no session is running.

## Where things live

**Placed files** (real files, owned in tam-bundle's record). On a systemd
rootstock such as Arch:

- `/usr/bin/tam`, `/usr/bin/tam-*` — every Tamlinux command;
- `/usr/share/man/man<N>/` — manual pages;
- `/usr/share/applications/`, `/usr/share/icons/hicolor/` — desktop
  entries and icons other programs look up;
- `/usr/lib/systemd/user/`, `/usr/lib/systemd/system/` and the vendor
  drop-in folders (`*.conf.d/`, `sysctl.d`, `tmpfiles.d`,
  `environment.d`, `system-sleep`, `zram-generator.conf.d`);
- `/usr/share/polkit-1/actions/`, `/usr/share/fontconfig/conf.*/`,
  `/usr/share/libalpm/hooks/` (Arch), `/usr/share/wayland-sessions/`.

A file is placed only when another program finds it by a standard path.
The allowed roots come from the tam nut's rootstock profile, installed by
the rootstock's package manager, never from the bundle, so a bundle cannot
widen them. A runit rootstock's profile lists its own folders instead of
the systemd ones. On every rootstock, nothing is placed in `/etc`,
`/usr/local`, `/opt`, `/var` or a home directory.

**The product tree, `/usr/lib/tamlinux/`:**

- `version/<bundle id>/` — one unpacked product tree: private programs,
  the desktop shell and its QML, plugins, fonts and image assets, defaults,
  and the product's own `BUNDLE` and `MANIFEST`. Real files. The current
  and previous versions stay unpacked; older ones are unpacked again from
  their bundle file when needed.
- `version/<bundle id>/.placed/` — that version's placed files, kept so a
  rollback has them without reading the bundle file.
- `current -> version/<bundle id>` — the one link. Programs compile in
  paths under `/usr/lib/tamlinux/current/`, never a version path.
- `staging/<bundle id>/` — exists only between prepare and apply.

Staging under `/usr/lib/tamlinux` and every placed root must be on one
mount of one filesystem (on Btrfs, one subvolume), so a staged file can be
renamed into place. Prepare compares device and mount id (`statx`
`STATX_MNT_ID`) for each root and refuses otherwise; device alone misses
bind mounts.

**System state, `/var/lib/tamlinux/`** (readable by everyone, nothing
secret in it):

- `bundles/<bundle id>.tbundle` — product bundle files, the portable form;
- `record` — every placed file: path, hash, bundle id;
- `transaction` — the system record of the switch in progress, if any
  (see "The transaction");
- `host-id` — the machine's identity, so a renamed or reinstalled host is
  still recognized. It is derived from the board's DMI UUID
  (`/sys/class/dmi/id/product_uuid`, readable only by root, so the system
  part reads it): SHA-256 of a fixed Tamlinux prefix and the UUID, so the
  raw hardware id never leaves the machine. A board that reports all
  zeros, all `F`s or another known placeholder falls back to a random id
  made once, and tam-bundle says so. A reinstall on the same hardware is
  the same host; a disk moved to another machine is a new one, so the
  target keeps its own host files;
- `keep`, `log`, `lock`, `stat/<bundle id>` — as before.

The earlier state directory `/var/lib/tam-bundle/` is read once and
folded into this one.

**Per-user state, `~/.local/state/tamlinux/`** (mode `0700`):

- `captures/<capture id>/` — settings captures, readable only by that
  user;
- `staging/`, `transaction` — the user's half of a switch (see "The
  transaction"), on the home filesystem so renames into home stay atomic;
- `settings.list` — paths the user added to the settings list.

**Build cache, `$XDG_CACHE_HOME/tamlinux/tam-bundle/`:** unchanged from the
earlier design.

## What a bundle contains

A product bundle file is one uncompressed POSIX tar archive (`pax`
format) with `BUNDLE` and `MANIFEST` first, so `verify` can stop early.
Uncompressed costs disk and saves CPU; anyone may compress it for
transport. Unpacked, it is the same directory as before and `verify`
checks either form.

```text
BUNDLE
MANIFEST
SETTINGS          settings list with marks (the cultivar's defaults)
tree/recipe/     the bundle file, component recipes and lock it was built from
tree/...         the rest of the product tree
placed/usr/bin/tam
placed/usr/share/man/man1/tam.1
...
```

A workstation bundle adds a settings capture:

```text
capture/CAPTURE   identity: user, host, host id, date, product it was taken with
capture/FILES     one line per listed path: mark, presence, mode, hash, origin, base, path
capture/files/... the files, relative to the home directory
```

**`BUNDLE`** (identity) keeps its earlier lines and gains the dependency
list and the tam-bundle version needed:

```text
format 2
name tamlinux
version 0.7.0
hash sha256:<64 hex>
source packaging <commit>
source libtam <commit>
source tam <commit>
tool cc gcc 16.2.1
buildenv sha256:<64 hex>   (exact versions of build-needs and the toolchain base)
tool tam-bundle 0.6.0
host alex-desktop <host-id>
target arch x86_64 baseline x86-64-v2
target libc glibc loader /lib64/ld-linux-x86-64.so.2
target profile arch-systemd
needs tam-bundle >= 0.6.0
needs file /usr/bin/Hyprland >= 0.56               arch=hyprland
needs file /usr/lib/libhyprbars.so                 arch=hyprland-plugin-hyprbars
needs file /usr/bin/quickshell >= 0.3.0            arch=quickshell void=quickshell
needs file /usr/lib/libdbus-1.so.3                 arch=dbus void=dbus
needs file /usr/bin/python3 >= 3.12                arch=python void=python3
needs qml Quickshell.Wayland 0.3                   arch=quickshell
needs python gi                                    arch=python-gobject
needs elf usr/bin/tam
after user-units-reload
after man-index
built 2026-11-10T18:00:00Z
```

**`MANIFEST`** lists both parts, sorted, one line per entry; the first
field says which part:

```text
format 2
p d 0755 -               usr
p d 0755 -               usr/bin
p f 0755 sha256:<64 hex> usr/bin/tam
p f 0644 sha256:<64 hex> usr/share/man/man1/tam.1
t d 0755 -               lib
t f 0644 sha256:<64 hex> share/fonts/...
```

`t` is the product tree, `p` placed. Placed entries are files and
directories only, never links. Tree entries may be relative links that
stay inside the tree, as before.

**Four identities**, each covering exactly this:

| Identity | Covers |
| --- | --- |
| **input key** | the lock, the recipes and the build-environment identity (see "The build environment"); names a cache entry |
| **bundle hash** | SHA-256 over `BUNDLE` without `hash` and `built`, then `MANIFEST`, then `SETTINGS`: the actual output |
| **capture hash** | `CAPTURE` and `FILES`, whose lines carry each file's hash |
| **transaction id** | one switch (see "The transaction") |

A matching hash proves a bundle is intact, not that anyone approved it
(see "Integrity is not permission").

**Edited product files on export.** A workstation export (`capture -o`)
refuses while installed product files differ from their bundle, and names
them. With `--save-edits` it first makes a marked **derived** bundle (a new
id, a `derived-from <bundle id>` line in `BUNDLE`, the edited files
listed) and exports that, so it never ships the original bytes and calls
them the running installation.

## The build environment

Three properties are kept apart. **Payload identity** is the bundle's hash.
**Reproducible rebuild** means the same lock and the same recorded build
environment give the same hash. **Offline install** needs only the bundle
file. Shipping a bundle guarantees the first and the third, not the second.

To make rebuilds honest without building a store:

- **Fixed PATH.** Builds run with `PATH=/usr/bin` only; the caller's PATH
  (development folders, Mise, Nix) is never passed in.
- **Offline build.** Fetching sources is a separate step before the build.
  The build runs in a new network namespace beside its user and mount
  namespaces, needing no root, so a recipe that tries to download fails
  loudly.
- **Build-environment identity.** Each recipe lists what it builds with
  (`build-needs`, checked by `build` the way `needs` is checked at
  install; users never need it). The cache key and `BUNDLE`'s `buildenv`
  line record the exact rootstock package versions of those packages and
  of the toolchain base (gcc, binutils, glibc, linux-api-headers, make,
  pkgconf). An unrelated rootstock update does not force a rebuild of
  everything; a toolchain change does.
- **Checked cache hits.** A cache entry's `done` stamp holds the key and
  the output's manifest hash. A hit is checked against the manifest with
  the stat-based quick check `check` uses, and fully rehashed under
  `--verify`; a mismatch rebuilds that entry.
- **Clean build root only if needed.** A clean Arch build root
  (`mkarchroot`, `makechrootpkg`) costs time and energy on every build. It
  is added only if second-workstation rebuilds of one lock do not match.

## The dependency list

Each `needs` line names a file the cultivar expects to find, because paths
are the same on every rootstock and package names are not.

- **Hints:** `arch=hyprland void=hyprland` are install hints, shown only in
  the complaint ("missing /usr/bin/Hyprland; on Arch: `pacman -S
  hyprland`"). tam-bundle never runs them; the user does.
- **Versions:** `>= <version>` by default. The version of a file is the
  version of the rootstock package that owns it, asked from the rootstock's
  package manager (`pacman -Qo`, `xbps-query -o`). A file no package owns
  (for example a build in `/usr/local`) is reported as "present, version
  unknown", a warning, not a refusal.
- **Exact:** `exact sha256:<hash>` where only the very build will do,
  which is only when Tamlinux itself compiles code against one build of a
  third-party program. Nothing does today: the `fred.*` plugins are
  Quickshell QML, and no Tamlinux program links to Hyprland's libraries.
  The one plugin ABI-locked to a Hyprland build, the hyprbars title-bar
  plugin, is third-party software from the rootstock and is listed like
  any other dependency; keeping it matched to Hyprland is the rootstock's
  job (on Arch, a machine-side pacman hook catches the mismatch). So a
  rootstock update that brings a newer Hyprland leaves the bundle valid,
  and a second workstation needs only a compatible Hyprland, not the same
  build.
- **tam-bundle itself:** `needs tam-bundle >= V` lets an older tam-bundle
  say "this bundle needs tam-bundle 0.6.0 or newer" and stop.
- **Where the list comes from:** the recipe declares it (the bundle file's
  `needs` lines, and a component recipe may add its own). At build time
  tam-bundle also checks what the built files actually use, and refuses to
  finish if something is used but not listed:
  - every ELF file's `DT_NEEDED` libraries and interpreter, read from the
    file, never by running it, resolved against the build host's library
    path; a library found inside the bundle needs no line;
  - every script's `#!` interpreter, including `/usr/bin/env <name>`.

  What cannot be read from files (Python imports, QML imports, commands a
  script runs) the recipe declares as probes; the `tam` and shell test
  suites are the backstop for anything a probe cannot see.
- **Target lines** come first and are checked first: architecture and CPU
  baseline, libc and its loader, rootstock profile. A mismatch refuses at
  once in plain words ("this bundle is for glibc; this machine uses
  musl"). A static tam-bundle runs anywhere, but the product it installs
  is dynamically linked and is only as portable as its target lines.
- **Probes** check more than a path, each cheap and read-only:
  - `needs qml <module> <version>`: the module's `qmldir` is found on the
    QML import path with at least that version;
  - `needs python <module>`: `python3 -I -c 'import <module>'` succeeds
    with the rootstock's Python;
  - `needs elf <path in bundle>`: every library that program needs
    resolves on this machine, read from the files, never by running the
    program.
- **When it is checked:** at prepare, again at apply (see "Recheck and
  secure"), before any rollback, after rootstock package changes (see
  below), and by `tam-bundle needs <bundle>` at any time. Missing entries
  are listed all at once, with their hints. Each check also covers the
  retained bundles, and `tam-bundle status` says whether the recovery
  anchor still meets its contract, so a rootstock upgrade that breaks the
  anchor is reported when it happens, not during an emergency. Recovering
  the rootstock itself is the rootstock's job (on Arch, Snapper root
  snapshots).
- **Rootstock package changes.** Bundles are not registered with the
  rootstock's package manager, so it does not know what Tamlinux needs. The
  tam nut, a native package, carries hooks that tell it: before a removal,
  if a package being removed owns a file the installed cultivar needs, the
  transaction is aborted with both named; after an upgrade, the contract is
  checked again and a break is reported ("Quickshell 0.4 no longer provides
  Quickshell.Wayland 0.3; the Tamlinux shell may not start"). On Arch these
  are pacman hooks; Void's xbps triggers come with its port.

## Settings

**What a capture holds** comes from the settings list: the cultivar ships
one (`SETTINGS`) and each user may add paths in
`~/.local/state/tamlinux/settings.list`. Each line marks a path relative to
home as `common` or `host`:

```text
format 1
common .bashrc
common .config/hypr/bindings.lua
host   .config/hypr/monitors.lua
host   .config/hypr/hypridle.conf
common .config/micro/
common .config/git/config
common .config/mimeapps.list
```

A directory entry covers the files beneath it. Three tiers decide the
list's scope:

- **In:** the settings that make up the Tamlinux experience: Hyprland,
  the desktop shell and `fred.*` plugins, terminals, the editor, the shell,
  git settings (not credentials), GTK and fonts, `mimeapps.list`,
  `environment.d`.
- **Out:** big application state: browser profiles, editor extensions,
  note vaults, `~/.local/share` application data and caches. A user may
  still add a single settings file from such a program.
- **Never on purpose:** secrets. Known secret paths are always refused by
  tam-bundle's code (`.ssh/`, `.gnupg/`, `.local/share/keyrings/`, agent
  credential folders such as `.gemini/`, `.codex/` and `.claude/`
  credentials, `.config/gh/hosts.yml`). Every other captured file is
  scanned for known secret patterns (private key blocks, GitHub, OpenAI,
  Anthropic, Slack and AWS token prefixes, `password=` and `token=`
  assignments); a match is excluded, and the capture names the file and
  the line number, never the value. The scan is a safeguard, not a
  guarantee: a secret in an unusual form can pass it. Captures and
  workstation bundles are therefore always private (see below).

**Presence: present, absent, excluded.** Every listed path has one line in
`FILES`, in one of three states:

| Presence | Meaning | Restore does |
| --- | --- | --- |
| `present` | the file and its contents are in the capture | writes it |
| `absent` | the path is listed but no file was there | removes the file: the only state that deletes anything |
| `excluded` | a file existed but was left out (refused path, secret pattern, unreadable, changed during the read) | leaves whatever is there alone and names it |

An `excluded` line records its reason, and for a secret pattern the line
number, never the value. A capture with any `excluded` line is marked
**incomplete**, and capture and restore both say which files and why.

**Private stays private.** Captures live in
`~/.local/state/tamlinux/captures/` (mode `0700`, files `0600`). A
workstation bundle written with `capture -o` is `0600`, and the message
says it holds personal settings. tam-bundle refuses to write a capture or
a workstation bundle anywhere under `/var/lib/tamlinux`, which is readable
by everyone and holds product bundles only. Workstation bundles are never
release material.

**Nothing from `/etc`.** Everything in `/etc` belongs to the machine and is
managed another way, never by Tamlinux bundles. A capture takes no file or
value from it: not locale, keyboard layout or timezone, and not `fstab`,
`crypttab`, hostname, machine-id, boot loader settings, saved Wi-Fi
passwords, users or passwords. The settings half of a switch therefore
never needs root.

The user's own preferences live in home and are captured as ordinary
`common` files: language in `~/.config/locale.conf` (read before
`/etc/locale.conf` by the rootstock's login profile) and the keyboard
layout in the Hyprland input settings, which fall back to the machine's
`/etc/vconsole.conf` layout when the user sets none. Timezone is where the
machine is, so it stays the machine's. When a captured `locale.conf` names
a locale the target has not generated, apply says so instead of letting it
fall back silently.

**Capturing while programs write.** Each file is read, hashed and then
`fstat`ed again; if its size or change time moved during the read, it is
read again, up to three times, then reported. A file a program rewrites
by itself (an editor's `settings.json`) is captured as it is at that
moment. A capture is coherent when no program is writing settings, which
is normally true at a switch because the desktop is down; otherwise it is
best-effort, says so, and names the files that changed while it was read.

**Host marks.** On the host a capture was made on, everything applies. On
another host:

- `common` files apply;
- for `host` files the target keeps its own; where it has none, it gets
  the cultivar's default from the product tree, if there is one;
- tam-bundle lists every file it skipped.

The unit is a whole file. When a `common` file names the capturing host or
one of its display outputs (`DP-1`, `HDMI-A-1`), capture warns that the
file looks mixed, so the user can split it once and mark the parts.

**Edits relative to the product.** `FILES` records, for each present
file, its origin (`default` when it equals the cultivar default, `edited`
otherwise) and its base, the product bundle id whose default it was
compared with. A capture never pretends an edited file came unchanged from
a source commit, and the base keeps the comparison correct after a
rollback or when a capture taken under one version is applied to another.

### Settings on an ordinary upgrade

Two parts, layering first.

**Layering.** Product defaults live in the product tree, under
`/usr/lib/tamlinux/current/`, and switch with the version like the rest of
the product. The user's files load them first and then override only what
the user changed: Hyprland's `hyprland.lua` loads the Tamlinux defaults
before the user's own files, `.bashrc` sources a Tamlinux file, and the
desktop shell and `fred.*` plugins read their defaults from the product
tree. The user's files hold only the user's changes, so an upgrade does not
touch them and nothing needs merging. Every settings file that has an
include or source mechanism uses it. Product defaults kept as copies in a
home directory are moved into the product tree.

**The three-way rule** covers listed files that cannot layer (for example
`mimeapps.list` and git settings). For each, tam-bundle compares the user's
file with the default of its base version and the default of the new
version:

| The user's file | The new default | Result |
| --- | --- | --- |
| missing | exists | the new default is installed |
| unedited | changed | replaced with the new default |
| edited | unchanged | kept, nothing said |
| edited | changed | kept; the new default is put beside it as `<name>.tamlinux-new` and listed in the ready message |
| unedited | dropped | removed |
| edited | dropped | kept and listed |

tam-bundle never merges file contents by itself.
`tam-bundle settings diff <path>` shows the user's file, the old default
and the new default, so the user can merge by hand. A `.tamlinux-new` file
is not itself on the settings list and is removed once the user's file
equals it or the user deletes it.

## Prepare and apply

A switch has two halves. **Prepare** does all the slow work while the user
keeps working. **Apply** does the few renames that make it live, when no
graphical session is running. Each half has a system part (root) and a
settings part (the user). Nothing here depends on the init system except
the boot trigger, which the tam nut supplies.

### The transaction

A switch is one transaction with one id, recorded twice: the system part in
`/var/lib/tamlinux/transaction`, the user's part in
`~/.local/state/tamlinux/transaction`. Both carry the same id, the old and
new product ids, the capture id, the user and the kind of switch (upgrade,
restore, product only, settings only). Each moves through five states, in
order, written atomically and synced:

| State | Meaning | After an interruption |
| --- | --- | --- |
| **staging** | prepare is copying and checking | never applied; the next prepare cleans up and starts again |
| **ready** | everything staged and synced; written last | the next `start` or boot applies it |
| **applying** | renames in progress | recovery goes forward |
| **activating** | files in place; required post-install steps running, each marked done | the remaining steps run again; each is safe to run twice |
| **complete** | everything required succeeded | the record moves to the log |

Only **ready** can be applied, and a record stays until **complete**, so
neither a half-prepared switch nor a half-activated one can pass for
finished. The first version supports one active Tamlinux user per machine;
the product is shared, and a second user's settings part is not defined
yet.

### Prepare (`tam-bundle install <bundle>`, `back`, `restore`)

1. **Check.** Verify the bundle. Run the dependency check; refuse with the
   full list if anything is missing. Refuse a newer bundle format. Note
   whether the host differs.
2. **Ownership.** For each placed path: if tam-bundle's `record` owns it,
   fine. If it is absent, ask the rootstock's package manager whether a
   package owns it anyway. If the path exists and is not tam-bundle's,
   refuse, naming the owning package or "a file no package owns". Nothing
   has changed yet.
3. **Names.** A placed command not named `tam` or `tam-*` is refused (see
   "Command names").
4. **Guarded files.** If the bundle adds, changes or removes a placed file
   that affects login or boot (see "Login and boot files"), list them and
   stop unless the user approves that exact list (`--approve-guarded
   <digest>`).
5. **Stage, system part (root, through `pkexec`).** First copy the bundle
   file into root-owned staging and verify that copy; everything after
   reads only the copy, so the file cannot be swapped between the check
   and its use. Unpack the product
   tree into `version/<id>/` (on Btrfs, clone unchanged files from the
   current version), copy placed files into `staging/<id>/`, hash every
   copy, then `syncfs` once. The system record says **staging** throughout.
   The old version stays unpacked and whole.
6. **Plan, settings part (the user).** Change nothing in home and stage no
   settings files: the user keeps working, and edits made from now on must
   not be lost. Do a dry run of the settings result and report it ("At the
   switch: 2 files you edited have new defaults (bindings.lua,
   mimeapps.list); you'll get .tamlinux-new copies"). Write the user's
   record with the plan: an upgrade, or a restore of a named capture.
7. **Publish.** Only when the system part is staged and synced and the
   user's plan is written, write both records as **ready** (the user's
   first, then the system's), each atomically and synced. Nothing in
   **staging** is ever applied.
8. **Tell the user**, warmly:

   ```text
   Tamlinux 0.7.0 is ready.

   We don't have to restart the whole computer. Just leave the desktop
   and type start again, and we'll get the house in order for you. When
   it comes back, you'll be amazed!
   ```

   Where the desktop starts some other way (a login screen), the message
   says to log out and back in instead.

Preparing again replaces an earlier switch that is still in **staging**
or **ready**, with a new transaction id. `tam-bundle cancel` removes one
in those states; nothing live has changed. Once a switch is **applying**,
it can only finish forward.

### Apply (when the desktop next starts, or at boot)

Linux has no general "run this at the next login" hook, and none is
needed: Tamlinux owns the commands that start a session. On a text-console
login the user types `start`, the tam nut's wrapper for `tam-start`, the
activities menu; its first entry runs `tam-desktop`, the desktop entry.
The text-only tier never runs `tam-desktop`, and a user may type
`tam-desktop` directly, so both `tam-start` and `tam-desktop` begin with
the trigger, one call to `tam-bundle apply --if-pending`:

1. No transaction record: go straight on (a stat or two). A record in
   **staging** is left alone and reported: an unfinished prepare, never
   applied.
2. **Clear the old version.** Stop the old version's own user services and
   timers, using the rootstock profile's step (on systemd, `systemctl
   --user stop` of the Tamlinux units). They are Tamlinux's and only
   waiting for their next run, so stopping them loses nothing. The desktop
   services are normally down already, because `tam-desktop` stops the
   graphical session target when the compositor exits; this step verifies
   that rather than assuming it. Then look for the user's own processes
   still using old files (see the busy check). Wait up to five seconds for
   them to finish; if some remain, name them and ask:

   ```text
   Two programs are still using the old Tamlinux:
     tmux (tty3): tam-watch
     bash (tty4): tam log
   Stop them and continue? [y/N]
   ```

   tam-bundle never stops the user's own processes without that yes. On
   no (the default), the command goes on with the old version as it is,
   the switch stays **ready**, and it is offered again next time. The
   trigger records which units it stopped; on no, or when the system part
   refuses or fails, it starts them again from the old version before the
   menu or desktop goes on.
3. Run `pkexec tam-bundle apply` (the system part). A polkit rule
   from the tam nut allows that one action, `org.tamlinux.bundle.apply`,
   without a password for a user in an active local session. It grants
   nothing new: apply only finishes what an authorized prepare already
   staged and armed.
4. Run `tam-bundle apply-settings` as the user (the settings part), then
   start the stopped timers and services again from the new version (the
   `user-units-reload` step).
5. If the switch replaced the running command's own file, re-execute the
   new one, so an old `tam-start` or `tam-desktop` never runs the new
   version.
6. Go on: show the menu, or start the compositor.

Leaving the desktop returns to the text-console shell, so a switch needs no
logout from the console, only `start` again.

**The boot trigger.** An interrupted apply must be finished before anyone
can log in. That requirement is the same everywhere; the mechanism is the
tam nut's and belongs to the rootstock:

- on systemd, a oneshot `tam-bundle-apply.service` ordered
  `Before=systemd-user-sessions.service`;
- on runit, a stage-1 script, which runs before any getty starts.

Both run the same `tam-bundle apply`. Neither is a bundle file.

The boot trigger's ordering does not make logins wait for its success, and
that is intended: logins must never depend on it, or a failed apply could
lock the user out of the console needed to repair it. A failed apply leaves
the record short of **complete**, so `tam-desktop` stays blocked (see
"Agreement, not absence"), and `tam-start` shows a repair screen in place
of the menu: what failed, `tam-bundle apply` to retry and `tam-bundle back`
to roll back. The text console and the user's shell always work.

**System part, `tam-bundle apply` (root).** Steps, under the lock:

1. No record, or a record in **staging**: exit at once (a stat or two).
   A **complete** record is moved to the log.
2. **Busy check.** If any graphical Tamlinux session is running, or any
   process still uses an old file, leave the switch armed, say which, and
   exit; the calling command then runs the old version as it is, and
   nothing is mixed. "Uses an old file" means a path in the old version's
   tree or an old placed file (such as `/usr/bin/tam-*`) appears in the
   process's program (`/proc/<pid>/exe`), mapped files (`maps`), command
   line, open files (`fd`) or working directory. Interpreted programs show
   only their interpreter as the program (`python3`, `quickshell`), so the
   command line and open files are what catch them; the shell service, for
   example, runs as `python3 /usr/lib/tamlinux/current/share/...`. The
   user-side trigger runs the same check first, so a yes can come before
   this one refuses. **The launcher chain is exempt, by process id, not by
   name:** the process running the trigger, its direct ancestors
   (`tam-desktop`, `tam-start`, the login shell) and the `pkexec` and
   `tam-bundle` processes it started. An unrelated old `tam-start` on
   another console is still caught. After apply, the exempted command
   re-executes itself as the new version.

   **New launches during apply.** A scan is a snapshot, so it is a
   diagnostic, not a guarantee. While the system record is **applying**,
   every Tamlinux program built on `libtam` refuses to start ("Tamlinux is
   switching versions; try again in a moment"); the check is one stat per
   program start, made only when a record exists. `tam-start` and
   `tam-desktop` wait for **complete** (see "The transaction"). Shell and
   Python programs that do not use `libtam` are not guarded: with no
   session running, the timers stopped and the renames taking about a
   tenth of a second, that window is small and is accepted for the first
   version. A full guarantee would need the renames to happen only at
   boot, which Q1 ruled out.
3. **Recheck and secure, still changing nothing live.** The rootstock may
   have changed since prepare. Rerun the ownership check for every placed
   path and the dependency check (one package-manager query and a stat per
   dependency); either failure refuses with the full list, and the record
   stays **ready**, so the old version keeps running. Then make sure every
   placed file about to be replaced has a rollback copy: the old version's
   `.placed/` normally holds it from that version's prepare, checked by
   hash against `record`. Where it is missing or differs, copy (on Btrfs,
   clone) the live file into `.placed/` and sync, with the live file left
   in place. If that copy fails, refuse; the record stays **ready**.
4. Set the system record to **applying** and sync.
5. For each placed change: rename the staged file over the destination
   (`rename(2)` replaces it in one atomic step, so the path always shows a
   whole old or a whole new file, and step 3 guaranteed a rollback copy).
   Placed files the new version no longer has are moved into the old
   version's `.placed/`. `fsync` each changed directory.
6. Replace `current` with a new link (`symlink` beside it, then `rename`
   over it) and `fsync` `/usr/lib/tamlinux`.
7. Update `record`, `keep` (the old id becomes "previous"), `stat`, `log`;
   set the system record to **activating**; `syncfs`.
8. Run the system post-install steps the rootstock profile defines, marking
   each done in the record as it succeeds. On systemd these are
   `system-daemon-reload`, `sysctl-apply` and `tmpfiles-create`
   (required) and `man-index` (optional); a runit profile has its own. A
   bundle names steps by these names and a step the profile lacks is
   reported, not guessed. Every step is safe to run twice.
9. When every required step has succeeded, set the system record to
   **complete**. A failed required step leaves it **activating** and is
   retried by the next apply. A failed optional step is logged and retried
   by `tam-bundle after`; it never blocks. Settings that only take effect
   at boot (logind, the system manager) are named in the message.

**Settings part, `tam-bundle apply-settings` (the user).** Run by the
session entry right after the system part, before the compositor; it never
needs root, because captures hold only home files. It only proceeds when
the system record is **complete** for the same transaction id. All
settings work happens here, when the desktop is down, so nothing made
after prepare is lost:

1. Capture the current settings automatically. This is the latest state,
   including edits made after prepare, and makes the switch undoable by
   rolling forward again.
2. Compute the result from the files as they are now: the three-way rule
   for an upgrade, the named capture for a restore. A file that changed
   since prepare's dry run is named in the message; its latest version is
   in the capture from step 1.
3. Stage the result in `~/.local/state/tamlinux/staging/` and sync, with
   the user's record in **applying**; rename each file into place and
   sync; then **activating** while the user post-install steps run
   (`user-units-reload` is required); then **complete**.

Settings are small home files, so this costs milliseconds. It never runs
as root and never follows links out of the home directory.

**Interruption.** Apply only moves files that prepare already wrote and
synced, so recovery always goes forward: rerunning apply reads the
record, and for each step checks whether it is already done (the
destination already has the new hash) or still to do (the staged file is
still there). The boot trigger and the session commands both rerun it
before any session starts. An apply that finds a state it did not produce
(a destination with neither hash) stops, changes nothing more, and says
what it found; `tam-desktop` then refuses to start the graphical session
and says how to recover from a text console.

**Agreement, not absence.** `tam-desktop` starts the new version only when
there is no record, or when the system record and the user's record carry
the same transaction id and both say **complete**. Any other combination
(one side **complete** and the other not, different ids, a record stuck in
**activating**) blocks the desktop with a message saying how to finish or
roll back from the text console, which always remains usable.

**What the user sees.** When the desktop starts after a pending switch:

```text
Getting the house in order: Tamlinux 0.6.21 -> 0.7.0 ... done.
Welcome back!
```

With nothing pending, nothing is printed and nothing measurable is spent.

## Restore and rollback

- `tam-bundle back` prepares the previous product version and the
  settings capture taken with it; `tam-bundle restore <bundle>` does the
  same for any retained bundle, forward or backward.
- Both always capture the current settings first, automatically, at the
  moment of the switch, not at prepare.
- `--product-only` keeps the current settings with the other software;
  `--settings-only` restores a capture onto the current software. When
  the capture was taken with another product version, tam-bundle warns
  before preparing.
- A restore makes the listed settings paths match the capture: `present`
  files are written, `absent` ones removed (they are in the automatic
  capture), and `excluded` ones left as they are and named. Paths not on
  the list are never touched.
- Product rollback to the previous version reads its unpacked tree and
  `.placed/`; anything older is unpacked from its bundle file first.

## Login and boot files

Some placed files can stop the machine from booting or the user from
logging in. The rootstock profile, not the bundle, lists them. On systemd:

- system units and drop-ins (`/usr/lib/systemd/system*`,
  `logind.conf.d`, `system.conf.d`, `system-sleep`), `sysctl.d`,
  `tmpfiles.d`, `zram-generator.conf.d`;
- polkit actions and rules; the Wayland session entry; the session
  commands `/usr/bin/tam-desktop` and `/usr/bin/tam-start`;
- anything under `udev`, `modules-load.d` or `modprobe.d`.

A change to any of them is named at prepare and needs the user's approval
of that exact list. The previous version stays unpacked as the rollback
point, and `sudo tam-bundle back` followed by `sudo tam-bundle apply`
works from a text console with no graphical session and no polkit agent.

## Command names

Every command Tamlinux places in `/usr/bin` is `tam` or `tam-*`; `build`
refuses any other name. That makes a collision with a distribution package
very unlikely, and the ownership check catches the rest. The current
exceptions are renamed: the activities menu `start` becomes `tam-start`,
and `suspend-unless-stay-awake` becomes a `tam-*` command. The old native
`tam-session`, a compatibility entry from the display-manager era, is
dropped; `tam-desktop` is the desktop entry.

**`hi` and `start`** must keep working on every Tamlinux workstation, each
leading to a `tam` command. The tam nut ships them as tiny real scripts,
not links: `/usr/bin/start` runs `tam-start` and `/usr/bin/hi` runs
`tam hi`. Before the cultivar is installed, both say so and run
`tam-nut welcome` instead, so they work from the moment the nut is
installed. Three layers keep it so:

- the rootstock's package manager owns both names through the tam nut
  package, and refuses another package that installs the same files;
- `tam-bundle build` refuses any name other than `tam` or `tam-*`, so a
  cultivar can never take these names over;
- `tam-bundle check` confirms that `hi` and `start` exist and lead to
  `tam hi` and `tam-start`, and reports it when they do not.

## Ownership

- **Tamlinux's files** are those in `record`. `tam-bundle where <path>`
  answers for them; `tam where` asks it first and falls back to the
  rootstock's package manager.
- **The rootstock's files** are those its package manager owns. Prepare
  asks it about every placed path tam-bundle does not already own and
  refuses on any owner. In the other direction, pacman refuses to install
  a package over a file it does not own ("exists in filesystem"), so a
  later rootstock package cannot silently overwrite a Tamlinux file.
- **Everything else** is left alone, wherever it is: `/usr/local`, extra
  native packages, per-user installations in home. tam-bundle writes only
  paths in `record`, staged placed paths that passed the check, its own
  state, and the settings-list paths in the user's home.
- tam-bundle does not register its files with the rootstock's package
  manager; that would mean generating native packages again.

## Commands

| Command | What it does | Root |
| --- | --- | --- |
| `tam-bundle build <recipes> <lock> -o <file> --version V` | Build a product bundle from exact commits | No |
| `tam-bundle lock <component> <commit>` | Set one component's commit in a lock file | No |
| `tam-bundle seal`, `verify <bundle>` | Write and check identity and file list | No |
| `tam-bundle needs <bundle>` | Check the dependency list on this machine | No |
| `tam-bundle capture [-o <file>] [--save-edits]` | Capture the user's settings; with `-o`, write a private workstation bundle | No |
| `tam-bundle install <bundle>` | Prepare a product or workstation bundle | Prepare's system part |
| `tam-bundle back`, `restore <bundle>` | Prepare the previous or a named bundle, with `--product-only` or `--settings-only` | Prepare's system part |
| `tam-bundle cancel` | Drop a switch in staging or ready | System part |
| `tam-bundle apply` | System part of a pending switch; run by the session entry and the boot trigger | Yes |
| `tam-bundle apply-settings` | Settings part; run by the session entry | No |
| `tam-bundle status` | Current, previous, the transaction and its state, space used | No |
| `tam-bundle check [--full]`, `where <path>` | Edited, missing or extra files; owner of one file | No |
| `tam-bundle keep`, `unkeep`, `prune [--yes]` | Protect, list and remove retained bundles | Yes |
| `tam-bundle settings diff <path>` | Show a settings file beside its old and new defaults | No |
| `tam-bundle after` | Rerun the user-session post-install steps | No |

Privilege works as before: run as a user, the root parts re-run through
`/usr/bin/pkexec` with `/proc/self/exe`; run as root from a text console,
they do the root part and print what the user still has to run. Refusals
(`126`, `127`) are explained. `tam-deploy` keeps the stage logic and calls
these commands.

**Integrity is not permission.** A privileged install runs only for the
exact bundle hash the caller names (`--digest`; `tam-deploy` passes the one
Fred approved for the stage), and its polkit action asks for
administrator authentication. Only `apply` of a switch that an
authorized prepare already made **ready** is password-free. Signatures for
sharing bundles across machines can be added later without a server.

## Retention

- **Always kept:** current, previous, the recovery anchor and anything
  pinned with `keep`.
- **Unpacked:** current and previous only.
- **Kept by reference:** anything reachable from the always-kept set, an
  open transaction (both its products and its capture) and every retained
  capture's base product, worked out under the lock.
  `tam-bundle status --why <id>` says why an item is kept ("base of
  capture fred@bosgame-2026-11-10-3f9a0c1b2d4e").
- **Removed only on request:** `tam-bundle prune` lists what it would
  remove and the space it would free, and removes only with `--yes`. A
  plain prune keeps the five most recent product bundles and the five most
  recent captures beyond the always-kept and referenced items. Nothing
  runs on a timer.
- After an install, tam-bundle mentions the space used when it passes a
  threshold (default 20 GB).

## Development builds and PATH

- Development builds install with `make install PREFIX=$HOME/.local`, so
  their commands land in `~/.local/bin`, ahead of `/usr/bin`. No root.
- `tam` and tam-bundle warn whenever a `tam` or `tam-*` command in
  `~/.local/bin` shadows the one in `/usr/bin`, naming both, so a
  forgotten development copy cannot hide.
- Programs compiled for the product tree look up private files under
  `/usr/lib/tamlinux/current/`; a development build that needs its own
  private files uses its own prefix for them, as today.

## Installing Tamlinux

Tamlinux reaches a machine in three steps:

1. **Install a rootstock** (Arch today, Void later) with its own installer
   and package manager.
2. **Install the tam nut.** The tam nut is the smallest piece of Tamlinux:
   one native package for the rootstock (`tam-nut`, built with `makepkg` on
   Arch and with `xbps-src` on Void later). It holds what is needed before
   any bundle exists, and nothing more:
   - `/usr/bin/tam-nut`, which welcomes the user and assesses the hardware;
   - `/usr/bin/start` and `/usr/bin/hi`, tiny scripts leading to
     `tam-start` and `tam hi` (see "Command names");
   - `/usr/bin/tam-bundle`, built static, so it needs no library and runs
     on glibc and musl rootstocks alike;
   - their manual pages, the polkit actions, and the polkit rule for
     `org.tamlinux.bundle.apply`;
   - the rootstock profile and the boot trigger for this rootstock's init
     system;
   - the empty `/usr/lib/tamlinux/` and `/var/lib/tamlinux/`;
   - the package-manager hooks that guard the installed cultivar's
     dependencies (see "The dependency list");
   - a dependency on polkit.
3. **From the tam nut, install the full Tamlinux cultivar.** `tam-nut`
   assesses the hardware, picks the graphical or text-only cultivar bundle,
   and runs `tam-bundle install`. The dependency check names any rootstock
   packages still missing, with their install hints. The user installs
   them and runs it again.

`tam` and its knowledge corpus come with the cultivar, text-only tier
included, not with the nut, so the native package never has to follow
`tam` releases.

`tam-nut` and `tam-bundle` are upgraded only through the tam nut package,
never by a bundle: the ownership check would refuse a bundle that tried to
overwrite a package-owned file anyway. A newer tam-bundle reads every older
bundle and capture format; an older one refuses a newer format by name.

**Static linking and users.** A static glibc program cannot use NSS
modules, so it cannot look up users that come from `systemd-homed`, LDAP or
similar. tam-bundle needs a user's home directory only for the settings
part, which runs as that user and takes `$HOME`; the root part needs only
the numeric id from `PKEXEC_UID` or `SUDO_UID`. To verify when building:
static against musl, or static glibc with no NSS lookups.

## From the earlier design

**Stays:**

- lock file, recipes, `build`, build cache, fixed build paths in user and
  mount namespaces, reproducibility test (tightened in "The build
  environment");
- `seal`, `verify`, `check`, `where`, `keep`, `prune`, the stat-based edit
  check and `--save-edits`;
- privilege through `pkexec` and the polkit action;
- line formats and "read every older format"; the `format 1` files remain
  readable;
- the crash-test harness (fault points, `SIGKILL` runs), applied to the new
  phases;
- the `libtam` additions (SHA-256, walk, records, atomic writes, directory
  sync, clone, safe tree removal, `flock`).

**Changes:**

- **Build prefix and split.** Components install with `PREFIX=/usr` into a
  private `DESTDIR`; anything under `usr/lib/tamlinux/current/` becomes the
  product tree, everything else must fall under an allowed placed root or
  the build is refused. Components keep compiling private paths under
  `/usr/lib/tamlinux/current/`.
- **Switch and rollback** become prepare and apply; the slot's `current/`
  and `kept/` become `version/<id>/` and the `current` link.
- **Manual pages** are placed in `/usr/share/man` and indexed by the
  rootstock's man-db (Arch runs `man-db.timer` daily) plus the
  `man-index` post-install step, which runs `mandb --quiet` at apply. The
  index built into the bundle (`index man`) no longer applies, because the
  pages now live in the shared hierarchy.
- **Native adapter.** `tamlinux-native`'s vendor drop-ins and commands
  move into the bundle as placed files; its dependency list moves into
  `needs`; what remains becomes the tam nut.
- **State** moves from `/var/lib/tam-bundle/` to `/var/lib/tamlinux/`.
- **Host:** `host any` / `host <name>` refusal becomes the host record
  and host marks.
- **Personal configuration** is now captured (it was excluded), from home
  only.
- **Init-specific pieces** (the apply trigger, allowed placed roots,
  login-and-boot list, post-install steps) move out of tam-bundle's code
  into the tam nut's rootstock profile.

**Goes:**

- `renameat2(RENAME_EXCHANGE)` of whole trees, and the recovery table
  built on it;
- `current/bin` on `PATH` and `current/share` on `XDG_DATA_DIRS`:
  `/etc/profile.d/tamlinux.sh`, `/usr/lib/environment.d/50-tamlinux.conf`
  and the `user@.service.d/20-tamlinux-bundle.conf` drop-in are removed;
  user units are placed in `/usr/lib/systemd/user/` instead.

## Proofs required

1. **Build and identity:** the same lock and recorded build environment
   build the same product hash twice; two captures of one version have
   different ids; the build-time link check refuses an unlisted library; a
   recipe that tries the network during the build fails; a corrupted cache
   entry is detected and rebuilt.
2. **Capture and restore:** change real settings, capture, then restore on
   a fresh user without consulting the original home or any Git
   repository; the secret scan excludes planted test tokens, and a restore
   leaves those excluded files untouched while removing an `absent` one.
3. **Second workstation:** install with only the rootstock, the tam nut
   and the bundle file. A missing dependency is named before
   anything changes; once supplied, the experience matches, host files
   skipped and listed.
4. **Forward and backward:** restore nonadjacent versions and different
   captures; verify placed files, removals, settings and post-install
   steps, and that nothing in `/etc` changed.
5. **Interruption:** a fault point at every durable transition (each
   record state change, each rename and sync, every hundredth file) for
   both parts, and `SIGKILL` at random moments; after each, the next apply
   finishes forward, or a record short of **complete** blocks the desktop,
   and `check --full` shows exactly the old or the new version. A power
   cut on real hardware during apply, at least once, before the daily
   cutover; it supplements the injected faults, never replaces them.
6. **Ownership:** a placed path owned by a rootstock package, and an
   unowned file at a placed path, are both refused before any change;
   files in `/usr/local`, an extra native package and a user's own
   `~/.local/bin` are untouched after install, restore and prune.
7. **Login:** `start` and `tam-desktop` with nothing pending add no
   measurable delay; with a pending switch they apply, re-execute
   themselves when the switch replaced them, then run the new version;
   Tamlinux timers and services are stopped and restarted from the new
   version; a user process using old files is named, and on no they run
   the old version and leave the switch armed, on yes it is stopped and
   the switch goes ahead; an interpreted old program (a Python script
   from the old tree) is caught. `hi` and `start` work before the
   cultivar is installed.
8. **Acceptance scenarios** (from the 2026-11-10 review): settings edited
   and native packages changed after prepare; interruption between the
   system and user parts; a failed required post-install step; a corrupt
   rollback copy; a launch during apply; a declined busy prompt; an
   excluded settings file beside an intentionally absent one; export with
   edited product files; a nonadjacent rollback after a rootstock upgrade.
9. **Repair and the way back:** repair from the text console with no
   graphical session and no polkit agent; and, before Nix is retired, a
   rollback from the first bundle-delivered version to the Nix and Home
   Manager generation, including Home Manager's refusal to overwrite the
   real settings files a bundle wrote.

## Migration order

Each step lands only on the developer's approval, one at a time, with a
rollback point. Nix and Home Manager stay the daily delivery until the
proofs pass. The first supported scope is Arch, glibc, systemd and one
active user; the Void profile is a shape in this design, not yet built.

1. **Design review.** This document, with the 2026-11-10 review.
2. **Tool, in three versions,** each in temporary roots with faults at
   every durable transition:
   - **0.6:** format 2, the transaction, prepare and apply;
   - **0.7:** settings: capture, presence, the three-way rule and the
     switch-time computation;
   - **0.8:** ownership, the runtime contract and probes, the build
     environment and the static build.
3. **Recipes.** `tamlinux-packages` builds with `PREFIX=/usr`, the
   placed/tree split, `needs` and `build-needs`; components that install
   outside the allowed roots are fixed.
4. **Tam nut package.** The `tam-nut` package replaces `tamlinux-native`,
   with its hooks and boot trigger; the dormant slot is converted to
   `version/<id>/` plus `current`.
5. **`tam-deploy` back end and candidate records,** so `fred-stage`, Test,
   Run and back use tam-bundle before any real delivery.
6. **Second workstation** (the sacrificial test host first): proofs 2 to
   9.
7. **Cutover on the daily workstation** through an ordinary Test of an
   exact candidate and Run on the developer's word. Home Manager's
   Tamlinux commands leave `~/.local/bin` (the shadow warning confirms none
   remain) and the Nix profile folders leave `PATH`.
8. **Recovery anchor, then Nix removal.** A bundle-delivered release
   becomes the anchor; then Nix and Home Manager retire, each step
   separately approved.

## Open

- **The polkit rule for `org.tamlinux.bundle.apply`:** confirm on Arch
  that a text-console login counts as an active local session and that
  the rule allows exactly that action without a password.
- **`tam-desktop` stops the graphical session target on exit:** confirm it
  does today; if not, that is a small product fix before the login proof.
- **A login-time switch without logind or elogind:** if a simpler startup
  later drops the session tracker polkit relies on, the login trigger needs
  another privilege path (a password, or switching only at boot); the boot
  trigger still works.
- **The settings list's first contents:** written from the files Home
  Manager links today, with marks, before the second-workstation proof.
- **Void:** its rootstock profile: the runit stage-1 boot trigger, placed
  roots and post-install steps, and `xbps-query` for ownership and
  versions. The product's own systemd files (user services, logind and
  sleep hooks, zram, tmpfiles, `environment.d`) need runit equivalents in a
  runit-built cultivar; that is product porting, not tam-bundle work.
