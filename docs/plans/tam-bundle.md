# tam-bundle — design

- **Status:** Approved 2026-11-09. `tam-bundle` 0.1.0 implements the
  formats, `seal`, `verify`, `needs`, `switch`, `back`, `recover`,
  `status`, `check`, `where`, `keep`, `prune` and `after`, with the crash
  and older-bundle tests. Still to come: `build` and `lock`, privilege
  through `pkexec`, the `man-index` step, the man page and `tam` pages, and
  the native adapter package.
- **Component:** `tamlinux-tools`, its own version number, C on `libtam`,
  linked statically.
- **Survey:** [2026-11-09 — File bundles, install records and atomic
  switching](../../upstream/2026-11-09-file-bundles-and-atomic-switch.md).
  Its seven candidate design elements are taken up below; one is changed
  (element 5, see "Files someone edited").

## The problem

Tamlinux is a set of its own files: commands, the desktop shell, plugins,
data, manual pages and user units. Today Nix builds them and Home Manager
links them into the home directory from the Nix store. That works, but:

- every installed file is a symlink into a store, so "where does this file
  come from?" is a chase through links;
- it brings a build daemon and sockets that Tamlinux otherwise avoids;
- a switch evaluates a whole language and takes minutes;
- most of what Nix does (complete dependency closures, binary caches,
  remote builders) Tamlinux does not need, because third-party software
  comes from the distribution's own packages.

What Tamlinux needs is smaller: package a coherent set of files built from
exact commits, mark it as one thing with a name, a version and a hash,
install it as real files in real folders, record what was installed, and
switch between versions with rollback.

## Requirements

1. **One bundle, one name.** A bundle has a name, a version, the exact
   source commits it was built from, and one content hash over all of it.
   The same sources give the same hash.
2. **Real files.** Installed files are real files at their final paths. No
   store to look through, no symlink chains.
3. **A readable record.** Plain text lists every installed file with its
   bundle, version and hash.
4. **Switch and roll back.** A switch either completes or leaves the old
   files in place. Rollback is one command.
5. **Nothing in the background.** No daemon, socket, timer or polling. It
   runs when typed and exits. No network unless asked.
6. **Fast.** A switch with nothing to rebuild takes seconds.
7. **Same on Arch and Void.** No dependence on pacman or xbps internals.
   It checks that needed native packages are present and names the missing
   ones.
8. **Safe with the user's files.** It never destroys a file it did not
   install. It refuses a bundle built for a different host.
9. **Small post-install steps, by name.** A fixed list of named steps, each
   recorded. No arbitrary scripts.
10. **Ours.** C on `libtam`, statically linked, with a man page, `tam`
    pages and its own tests.

**Out of scope:** a general package manager for third-party software,
building third-party software, binary caches, remote servers, and a user's
own configuration files (see "Personal configuration").

## Verified first: exchanging two directories

The design rests on `renameat2(RENAME_EXCHANGE)`, which swaps two existing
paths in one step. A small C probe was run on Linux 7.2 on Btrfs and on
ext4, both mounted normally and writable without root. Both passed every
check:

- two non-empty directory trees exchange in one call;
- afterwards the live path holds only the new tree, and the old tree is
  complete at the other path;
- a file opened before the exchange still reads the old contents;
- exchanging again restores the old tree;
- an exchange with a missing path fails with `ENOENT`, rather than turning
  into a plain move;
- an exchange across filesystems fails with `EXDEV`.

The same probe tested file clones (`FICLONE`, "reflinks"): supported on
Btrfs, `EOPNOTSUPP` on ext4. The design therefore clones on Btrfs and
copies elsewhere.

**Consequence for the layout:** both paths in an exchange must be on the
same filesystem, and on Btrfs in the same subvolume. Keeping the live tree,
the staging tree and the kept trees under one parent directory guarantees
that.

## Words used here

- **Bundle:** a built set of files plus its identity and file list, ready
  to install. It is a directory; it can be copied anywhere and verified.
- **Slot:** the directory a bundle is installed into. The Tamlinux product
  uses the slot `/usr/lib/tamlinux`. Tests and private tools can use other
  slots, including ones under a home directory that need no root.
- **Live tree:** `<slot>/current/`, the files in use.
- **Kept tree:** a previous live tree kept whole for rollback, at
  `<slot>/kept/<bundle id>/`.
- **Bundle id:** `<name>-<version>-<first 12 hex digits of the hash>`, for
  example `tamlinux-0.7.0-3f9a0c1b2d4e`.

## Where things live

**The product slot, `/usr/lib/tamlinux/`** (one Tamlinux-owned namespace
under `/usr`):

- `current/` — the live tree. Real directories and real files:
  - `current/bin/` — commands; this directory goes on `PATH`.
  - `current/lib/` — private programs, QML modules, plugins.
  - `current/share/` — data, desktop entries, icons, fonts, manual pages
    (`current/share/man`), user units (`current/share/systemd/user`).
  - `current/BUNDLE` and `current/MANIFEST` — the identity and file list
    of the bundle that is live. Because they are inside the tree, they
    change in the same step as the files, so the live tree always says
    exactly what it is.
- `kept/<bundle id>/` — previous trees, each with its own `BUNDLE` and
  `MANIFEST`.
- `staging.<bundle id>/` — exists only during a switch.

Components are built with their prefix set to `/usr/lib/tamlinux/current`,
so every path compiled into a program points at the live path, never at a
kept or staging path. A kept tree is only ever used after it has been
exchanged back into `current/`.

**State, `/var/lib/tam-bundle/<slot name>/`:**

- `lock` — taken with `flock` for the length of any changing command.
- `switch` — the intent file, present only while a switch is in progress.
- `log` — one line per switch, rollback, prune and post-install step.
- `keep` — bundle ids that `prune` must not remove, each with a reason
  (current, previous, recovery anchor).
- `stat/<bundle id>` — size, inode, modification time and change time
  of each live file (a change time cannot be set back by hand), recorded after a switch, so a check can find edited files without
  hashing everything.
- `saved/` — files saved before a switch replaced them (see "Files someone
  edited").

For a slot owned by a user other than root, state lives in
`$XDG_STATE_HOME/tamlinux/tam-bundle/<slot name>/`, following `libtam`'s
per-command state path.

**Fixed paths outside the slot** (session entry, PAM, polkit, system
units, environment settings) do not belong to `tam-bundle`. The native
adapter package owns them (see below). `tam-bundle` writes only inside its
slot and its state directory.

**Build cache, `$XDG_CACHE_HOME/tamlinux/tam-bundle/`:** finished builds
of each component keyed by commit and tool versions, so rebuilding a bundle
reuses components whose commit did not change. Disk is spent to save time.

## Formats

All formats are plain UTF-8 text, one record per line, readable with
`less`. Paths are relative to the tree, never contain a newline, tab or
`..` component, and come last on a line so spaces are allowed. Each file
starts with a `format` line; `tam-bundle` reads every format up to its own
and refuses a newer one with a message naming the version it needs.

**Lock file** (`tamlinux.lock`, in the packaging repository; replaces the
Nix flake lock). One line per component, the exact commit in full:

```text
format 1
component libtam         https://github.com/greenermoose/libtam         <40-hex commit>
component tamlinux-tools https://github.com/greenermoose/tamlinux-tools <40-hex commit>
component tam            https://github.com/greenermoose/tam            <40-hex commit>
component tamlinux       https://github.com/greenermoose/tamlinux       <40-hex commit>
```

`tam-bundle lock <component> <commit>` rewrites one line; nothing updates
the lock implicitly.

**Recipe** (one small file per component, `recipes/<component>.recipe`,
data only):

```text
format 1
component tam
build make
make-vars PREFIX=/usr/lib/tamlinux/current
after-build libtam
```

`build make` means: export the commit with `git archive` into a clean
build directory, run `make` and then `make install DESTDIR=<bundle tree>`
with the listed variables, `SOURCE_DATE_EPOCH` set to the commit time and
`LC_ALL=C`. `after-build` orders components; a build-only component such as
`libtam` installs into a private build prefix and contributes no files to
the bundle. There is no other build kind in the first version, and no
free-form shell.

**`BUNDLE`** (identity):

```text
format 1
name tamlinux
version 0.7.0
hash sha256:<64 hex>
source libtam <commit>
source tam <commit>
tool cc gcc 16.2.1
tool tam-bundle 0.1.0
host any
needs command quickshell   arch=quickshell void=quickshell
needs file /usr/lib/libdbus-1.so.3   arch=dbus void=dbus
after man-index
after user-units-reload
after shell-restart
built 2026-11-09T18:00:00Z
```

- `host any` or `host <hostname>`; a bundle for another host is refused.
- `needs` lines are checked by the presence of a command or file, not by
  asking the package manager. The package names are only used in the
  message that names what to install.
- `after` lines name post-install steps from the fixed list.
- `built` is information and is not part of the hash.

**`MANIFEST`** (file list, sorted by path, bytewise):

```text
format 1
d 0755 -                    bin
f 0755 sha256:<64 hex>      bin/tam
f 0644 sha256:<64 hex>      share/man/man1/tam.1
l -    ../../bin/tam        share/tamlinux/bin/tam
```

Types are `d` (directory), `f` (regular file) and `l` (symbolic link).
Links are allowed only inside the tree and only with relative targets that
stay inside it; the build refuses any other link. Owner is always root (or
the slot owner for a home slot) and is not recorded.

**The bundle hash** is SHA-256 over `BUNDLE` without its `hash` and
`built` lines, followed by `MANIFEST`. Identical sources and tools give an
identical hash only if every component builds reproducibly; the test suite
builds a bundle twice and compares hashes, and a component that fails is
fixed rather than excused.

**Log** lines:

```text
2026-11-09T18:02:11Z switch tamlinux-0.7.0-3f9a0c1b2d4e from tamlinux-0.6.20-a1b2c3d4e5f6 ok 1.8s
2026-11-09T18:02:12Z after man-index ok
2026-11-09T18:40:03Z back tamlinux-0.6.20-a1b2c3d4e5f6 from tamlinux-0.7.0-3f9a0c1b2d4e ok 0.1s
```

## Commands

| Command | What it does | Root for the product slot |
| --- | --- | --- |
| `tam-bundle build <recipes> <lock> -o <dir>` | Build a bundle from exact commits | No |
| `tam-bundle seal <dir> --name N --version V ...` | Write `BUNDLE` and `MANIFEST` for a built tree; `build`'s last step, and how tests make bundles | No |
| `tam-bundle verify <bundle>` | Check every file against `MANIFEST` and the bundle hash | No |
| `tam-bundle needs <bundle>` | Report missing native commands or files, with package names | No |
| `tam-bundle switch <bundle>` | Install the bundle and make it live | Yes |
| `tam-bundle back [<bundle id>]` | Make the previous (or the named) kept tree live again | Yes |
| `tam-bundle status` | Live bundle, previous, kept trees, interrupted switch if any | No |
| `tam-bundle check [--full]` | Find edited, missing or extra files in the live tree | No |
| `tam-bundle where <path>` | Bundle, version and hash for one installed file | No |
| `tam-bundle keep <bundle id> <reason>` / `unkeep` | Protect a kept tree from pruning | Yes |
| `tam-bundle prune [--yes]` | List removable kept trees; remove only with `--yes` | Yes |
| `tam-bundle recover` | Finish or undo an interrupted switch | Yes |
| `tam-bundle lock <component> <commit>` | Set one component's commit in a lock file | No |
| `tam-bundle after` | Run the live bundle's post-install steps as the invoking user | No |

Every changing command takes `--slot <dir>` (default `/usr/lib/tamlinux`)
and `--dry-run`. Every command runs `recover`'s check first and stops if a
previous switch was interrupted and cannot be finished automatically.

**Privilege.** Run as a user, `switch`, `back`, `keep`, `prune` and
`recover` re-run their privileged part through `pkexec tam-bundle ...`
(the native adapter ships the polkit action), then run user-session
post-install steps as the user. Run directly as root (for example with
`sudo` from a text console, where no polkit agent runs), they do the
privileged part and print the user-session steps still to run. A home slot
needs no privilege at all.

**Relation to `tam-deploy`.** `tam-deploy` keeps the stage logic (Test,
Run, back, status) and calls `tam-bundle` for building, switching and
rollback. `tam where <path>` asks `tam-bundle where` first and falls back
to the distribution's package manager.

## Switch algorithm

Inputs: a verified bundle, the slot, the state directory.

1. **Lock and check.** Take `lock`. Confirm `current/`, `kept/` and the
   staging path are on the same device (`st_dev`); refuse otherwise. Run
   the recovery check. Read `current/BUNDLE`; if the live hash equals the
   bundle's, report "already live" and stop. Refuse a wrong `host`. Run the
   `needs` check and refuse with the list of what is missing.
2. **Edits.** Run `check` on the live tree (see "Files someone edited").
   Stop if it finds edits, unless `--save-edits` was given.
3. **Intent.** Write `switch` (`from <live id>`, `to <new id>`, `staging
   <path>`) with `tam_fs_write_atomic` and sync the state directory.
4. **Stage.** Create `staging.<new id>/`. For each `MANIFEST` entry, in
   order:
   - directory: create it with its mode;
   - file whose hash equals the live file's recorded hash: clone it from
     `current/` (`FICLONE`); where cloning is unsupported, copy it from the
     bundle;
   - other files: copy from the bundle, hash the copy, and stop on a
     mismatch; bundle files are opened strictly beneath the bundle
     directory (`openat2` with `RESOLVE_BENEATH` and
     `RESOLVE_NO_SYMLINKS`), so a link swapped into a bundle cannot make
     a root switch read files outside it;
   - link: create it.
   Then write `BUNDLE` and `MANIFEST`, set directory modes last, and call
   `syncfs` on the staging tree once, so every new file is on disk before
   any of it becomes visible.
5. **Exchange.** `renameat2(staging.<new id>, current, RENAME_EXCHANGE)`,
   then `fsync` the slot directory. The new tree is live; the old tree is
   now at `staging.<new id>/`.
6. **Keep the old tree.** Rename `staging.<new id>/` to `kept/<old id>/`
   and `fsync` both directories.
7. **Record.** Append the log line, write `stat/<new id>`, add the old id
   to `keep` as "previous" (replacing the last "previous"), remove
   `switch`, and sync the state directory.
8. **Post-install steps.** Run each `after` step and log its result. A
   failed step is reported and logged; it does not undo the switch, because
   the files are already correct and each step can be rerun.

**Rollback (`back`)** needs no staging and copies nothing: write the
intent, exchange `kept/<previous id>/` with `current/` directly, rename the
displaced tree to `kept/<newer id>/`, record, and run the post-install
steps. Before exchanging, `back` runs `check` on the kept tree; with
`--full` it hashes every file. It takes well under a second.

**Running programs.** A program that already has a file open keeps reading
the old file (verified above). A long-running program that opens files by
path later, such as the desktop shell loading QML, would read the new tree
while running old code. The `shell-restart` step exists for that, and
`tam-deploy` already restarts the shell after a change.

## Interruption recovery

The intent file and the identity file inside the live tree decide every
case. At the start of every command:

| Found | Meaning | Action |
| --- | --- | --- |
| No `switch` file, no `staging.*` | Clean | Continue |
| No `switch` file, a `staging.*` directory | A staging tree that never went live | Remove it, log it, continue |
| `switch` present, `current/BUNDLE` is `from` | Stopped before the exchange | Remove staging, remove `switch`, log "undone"; the old tree is live and untouched |
| `switch` present, `current/BUNDLE` is `to` | Stopped after the exchange | Do steps 6 and 7, log "finished", then report that post-install steps did not run and run them |
| `switch` present, `current/BUNDLE` is neither | Not produced by this program | Stop, change nothing, print what was found |

Removal of staging or kept trees uses directory descriptors (`openat`,
`unlinkat`), never follows symbolic links and never crosses to another
device.

**Crash test (required before any real use).** The test build has a fault
point after each numbered step and after every hundredth file in step 4;
a test variable makes the program exit at a chosen point without cleanup.
For each point the suite checks that the live tree afterwards is exactly
the old bundle or exactly the new one (by `check --full`), and that the
next command recovers to a clean state. A second test sends `SIGKILL` at
random moments during a hundred switches and rollbacks in a temporary
slot. Power loss is not simulated in this version; it relies on step 4's
`syncfs` and the directory `fsync`s.

**Older-bundle test (required).** Switch through at least three bundles,
including one written in the oldest supported format, then roll back to
each and `check --full` it.

## Files someone edited

The survey proposed pacman's three-hash rule for user-edited files
outside the exchanged directories. In this design there are no such files:
the slot holds everything `tam-bundle` installs, and fixed paths elsewhere
belong to the native adapter package, so the distribution's own rules
(for example pacman's `.pacnew`) cover them.

What remains is someone editing a file inside `current/` by hand, as root,
for example while debugging. An exchange never overwrites such a file; it
moves with the old tree into `kept/`. But a later `prune` would delete it.
So:

- `check` compares size, modification time and inode with `stat/<id>`,
  and hashes only files whose values changed (`--full` hashes everything).
  It lists edited, missing and extra files.
- `switch` stops when `check` finds any, and names them.
- `switch --save-edits` copies them to `saved/<old id>/<path>` first, logs
  that, and continues.

## Native adapter package

A small native package per distribution: `tamlinux-native` on Arch, a
package of the same name built with `xbps-src` on Void. It carries what the
distribution must know about, and nothing that changes with each Tamlinux
version:

- `/usr/bin/tam-bundle`, built from a pinned `tamlinux-tools` commit. It is
  never inside the bundle it installs, so a broken bundle cannot break the
  tool that rolls it back.
- The empty slot `/usr/lib/tamlinux/` and its `kept/` directory.
- The polkit action that lets `pkexec tam-bundle` run the privileged part.
- Environment settings that put `current/bin` on `PATH`, `current/share`
  on `XDG_DATA_DIRS` and `current/share/man` on the manual path, in the
  distribution's vendor location (`/usr/lib/environment.d/` for the
  session).
- The Wayland session entry, PAM configuration and any system units, all
  pointing at stable paths under `current/`.
- Ordinary dependencies on the third-party packages Tamlinux needs, so
  pacman or xbps installs them.

`pacman -Qo` knows the adapter's files and not the bundle's; `tam where`
answers for both.

## libtam additions

In the next `libtam` minor version, with tests:

- **SHA-256:** incremental (`init`, `update`, `final`), whole file, and
  hex formatting.
- **Directory walk:** sorted, descriptor-based (`openat`, `fstatat`), never
  following symbolic links, with a callback per entry.
- **Line records:** read and write the line formats above, with path
  validation (no newline, tab or `..`) and a `format` line check.
- **File system:**
  - `tam_fs_write_atomic` also syncs the parent directory (today it syncs
    the file but not the directory that holds the new name);
  - sync a directory;
  - exchange two paths, reporting `EXDEV` and unsupported filesystems
    clearly;
  - clone a file, falling back to `copy_file_range` and then to read and
    write;
  - remove a tree under a directory descriptor, without following links or
    crossing devices;
  - take and release an `flock` lock with a timeout.

`tam-bundle` itself holds only its own logic: formats, the switch,
recovery, checks and the command line.

## Post-install steps

The first fixed list. Each runs only if the bundle names it, and each is
logged:

- `man-index` — rebuild the manual index for `current/share/man`.
- `user-units-reload` — `systemctl --user daemon-reload` in the invoking
  user's session.
- `qml-cache-purge` — remove the desktop shell's compiled QML cache.
- `shell-restart` — restart the desktop shell if it is running.

Adding a step is a code change to `tam-bundle`, reviewed like any other.

## Size and speed

Today's Tamlinux commands output is about 400 MB in 1,115 files, mostly
fonts (about 280 MB) and image assets (about 115 MB) that rarely change.

- **Btrfs:** unchanged files are clones, so a switch writes only changed
  files and a kept tree costs only the disk of the files that differ.
- **ext4 and others:** each switch copies the whole tree once (a second or
  two on solid-state storage) and each kept tree costs its full size. With
  current, previous and one anchor kept, that is about 1.2 GB.
- **Rollback:** an exchange of existing trees, under a second on any
  filesystem.
- **Build:** unchanged components come from the build cache.

`prune` lists sizes as reported by the filesystem; on Btrfs, shared extents
mean the space actually freed can be much smaller than the listed size, and
the listing says so.

## Migration order

Each step lands only on the developer's approval, one at a time, each with
a rollback point.

1. **Library and tool.** The `libtam` additions with tests and a version
   bump; then `tam-bundle` with the crash and older-bundle tests, run only
   in temporary slots, with a development install.
2. **Recipes.** Recipes and a lock file in `tamlinux-packages` that build
   the same components the current Nix packages build. Compare the
   resulting file list with today's delivered file set and explain every
   difference.
3. **Native adapter.** Build and install `tamlinux-native`. Switch a first
   bundle into `/usr/lib/tamlinux` while the current delivery stays active
   and first on `PATH`; nothing changes for the session yet.
4. **Readers of the version.** `tam` and the shell read the Tamlinux
   version from `current/BUNDLE` instead of a per-user version file.
5. **Cut-over.** The environment puts `current/bin` ahead of the old
   delivery. Undoing the cut-over is reordering the environment.
6. **`tam-deploy` back end.** Test and Run go through `tam-bundle`, from
   one recorded bundle to one endpoint, as today.
7. **Third-party commands.** Commands that came with the old delivery move
   to native packages or Mise, or are dropped.
8. **Recovery anchor.** A release delivered by `tam-bundle` becomes the
   kept recovery anchor.
9. **Removal.** Only after the anchor has proved itself, the old delivery
   mechanism is removed, one approved step at a time.

## Personal configuration

A user's own configuration files are not bundled. They stay real files in
the home directory, and the user keeps their history in their own version
control. `tam-bundle` never writes in a home directory except into a home
slot the user named.

## Open

- **Files programs rewrite.** Some programs rewrite their own settings
  files (for example an editor's or an agent's `settings.json`). For each
  one: track it and accept the program's changes as visible differences,
  or keep the user's settings in a separate file the program reads. Decided
  file by file.
- **Which files leave the old delivery for the user's own repository.**
  The exact split between product files (into the bundle) and personal
  files (into the user's own version control) is decided per file before
  step 5.
- **User units.** Whether the user service manager finds units in
  `current/share/systemd/user` through `XDG_DATA_DIRS` depends on that
  variable being set in the manager's own environment at start, not only
  for its services. This needs a test before step 3; the fallback is a
  small fixed set of unit files in the native adapter that start programs
  under `current/`.
- **Login shells on a text console.** The vendor environment directory
  covers the graphical session. A login shell on a text console reads
  `/etc/profile`, where Tamlinux ships no files. Either the user's own
  shell profile adds `current/bin`, or the adapter ships one documented
  exception.
- **Copying bundles between computers.** Any copy method works because
  `verify` checks the result; whether to add a `pack` command that writes
  one archive file is left until a second computer needs it.
- **Exporting bundles as native packages** for a public repository: later,
  not built now.

## Acceptance

- The crash test and the older-bundle test pass in temporary slots on
  Btrfs and ext4.
- Building the same lock twice gives the same bundle hash.
- A switch with no changed component, from a warm build cache, takes
  seconds; a rollback takes under a second.
- `tam-bundle` installs, switches and rolls back Tamlinux on a workstation
  as real files, with no Nix or Home Manager present.
- `tam where <path>` answers for every file in the live tree.
