# tam-bundle — design

- **Status:** Approved 2026-11-09. `tam-bundle` 0.1.0 implemented the
  formats, `seal`, `verify`, `needs`, `switch`, `back`, `recover`,
  `status`, `check`, `where`, `keep`, `prune` and `after`, with the crash
  and older-bundle tests; 0.2.0 adds `build` and `lock`; 0.3.0 adds
  privilege through `pkexec` and the polkit action, and 0.3.1 records the
  user's session-step results; 0.4.0 builds the manual index into the
  bundle (`index man`). The manual page `man/tam-bundle.1` is installed
  with it and checked against `--help` by `make test`, and the `tam` corpus
  has a page. 0.4.1 makes every changing command recover first (see
  "Interruption recovery"). 0.5.0 adds explicit make and install targets,
  pinned packaging files and the packaging source commit. Still to come:
  full product recipes and the native adapter package.
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
- `session.<uid>` — results of the session steps a root run left for that
  user, who cannot write `log`. Only the root run creates it, owned by that
  user; the user only appends to it. `status` shows its last line.
- `keep` — bundle ids that `prune` must not remove, each with a reason
  (current, previous, recovery anchor).
- `stat/<bundle id>` — size, inode, modification time and change time
  of each live file (a change time cannot be set back by hand), recorded after a switch, so a check can find edited files without
  hashing everything.
- `saved/` — files saved before a switch replaced them (see "Files someone
  edited").

The state directory and its files are readable by everyone (directories
`0755`, files `0644`), so `status`, `check` and `where` need no privilege;
nothing in them is secret.

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

- `build/<component>-<key>/` — one finished component: `out/` (its
  installed files), `log` and the `done` stamp, written last; an entry
  without `done` is rebuilt. The key is SHA-256 over the recipe, the
  commit, the output kind, the prefix, the tool versions and the keys of
  the components named in `after-build`, so a changed dependency rebuilds
  its dependants.
- `git/<component>.git` — a bare mirror, fetched into only when it lacks a
  locked commit and `--repos` did not supply it.
- `lock` — one build at a time.

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

`tam-bundle lock <component> <commit>` rewrites one line, written with
single spaces, and leaves comments and the other lines as they were;
nothing updates the lock implicitly. Adding a component needs `--url`. A
revision shorter than a full commit is resolved only with `--repos DIR`, in
the local clone `DIR/<component>`, never on a server.

**Recipe** (one small file per component, `recipes/<component>.recipe`,
data only):

```text
format 1
component tam
build make
make-vars LIBTAM_PREFIX=@libtam@
after-build libtam
```

```text
format 1
component libtam
build make
output build-only
make-targets all display session
install-targets install install-display install-session
```

`build make` means: export the commit with `git archive` into a clean
build directory, run `make -j<CPUs>` and then `make install` with the
listed variables. tam-bundle sets `PREFIX` itself, to `<slot>/current` (the
default slot gives `/usr/lib/tamlinux/current`), installs with
`DESTDIR=<private directory>`, and refuses anything installed outside the
prefix. Each build sees only `PATH` from the caller, plus
`SOURCE_DATE_EPOCH` set to the commit time, `LC_ALL=C`, `TZ=UTC`, its own
`HOME` and `TMPDIR`, and `PKG_CONFIG_PATH` naming its build-only
dependencies. A recipe may not set `PREFIX`, `DESTDIR` or
`SOURCE_DATE_EPOCH`.

`make-targets` lists the targets for the first make (default: the
Makefile's default target); `install-targets` lists the targets for the
second (default: `install`). Both accept target names only, never options
or variable assignments. Each list runs in one make invocation, with the
same prefix and variables. The recipe bytes, including these lists, are
part of the build cache key.

**Fixed build paths.** A build directory can end up inside what is built:
debug information records it, a static library carries it into every
program that links it, and a pkg-config file names its prefix. So each
`make` runs in its own user and mount namespace (no root needed; the
caller's user and group map to themselves), where `/tmp` is a new, small
tmpfs holding only fixed mount points:

- `/tmp/tam-bundle/work` — the component's cache entry: `src`, `home`,
  `tmp` and `dest`;
- `/tmp/tam-bundle/deps/<component>` — each build-only component it builds
  against, directly or through another, mounted read-only; a build-only
  component's own build sees its prefix here, writable, and installs with
  this `PREFIX`.

The same lock therefore builds the same bytes whatever `$XDG_CACHE_HOME`
is. Found 2026-11-09: `libtam` compiles with `-g`, and before this a build
under another cache directory gave another hash. `libtam` 0.5.1 also maps
its build directory out with `-ffile-prefix-map`, as "a component that
fails is fixed rather than excused" asks. `build` needs unprivileged user
namespaces, which Arch's and Void's kernels allow; where they are turned
off, `build` stops and says so.

`after-build` orders components. A build-only component (`output
build-only`) installs into a private prefix in the build cache and
contributes no files to the bundle; `@<component>@` in a make variable
names that prefix, and must name a build-only component listed in
`after-build`. Two components installing the same path, or the same
directory with different modes, are refused. There is no other build kind
in the first version, and no free-form shell.

**Bundle file** (`recipes/bundle`), the bundle-wide part of `BUNDLE`; the
version comes from `build --version`:

```text
format 1
name tamlinux
host any
needs command quickshell   arch=quickshell void=quickshell
index man
after shell-restart
file notes/tamlinux-0.7.0.txt share/tamlinux/version-notes.txt
```

`index man` asks `build` for the manual index (see "The manual index").

`file <packaging path> <tree path>` adds a regular data file from the
packaging repository's HEAD commit, installed as mode 0644. Both paths
are relative; the source is a single word and the destination may contain
spaces. Parents are created without following links, and existing output
is never replaced. Links, missing files and the reserved destinations
`BUNDLE` and `MANIFEST` are refused.

For recipes inside a Git work tree, the bundle file, component recipes
and lock must all match HEAD in that same repository. `BUNDLE` records
`source packaging <commit>` alongside the component sources. Extra files
always come from that commit, even if their working copies have edits.
Standalone scratch recipes outside Git still work, without a packaging
source line, but cannot use `file`. `packaging` is reserved as a component
name when recording a packaging source.

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
tool man-db 2.13.1
host any
needs command quickshell   arch=quickshell void=quickshell
needs file /usr/lib/libdbus-1.so.3   arch=dbus void=dbus
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
2026-11-09T18:02:12Z after user-units-reload ok
2026-11-09T18:40:03Z back tamlinux-0.6.20-a1b2c3d4e5f6 from tamlinux-0.7.0-3f9a0c1b2d4e ok 0.1s
```

## Commands

| Command | What it does | Root for the product slot |
| --- | --- | --- |
| `tam-bundle build <recipes> <lock> -o <dir> --version V` | Build a bundle from exact commits, for the slot's live tree | No |
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
and `--dry-run`. Every changing command recovers an interrupted switch first,
and stops if it cannot be finished or undone automatically (see
"Interruption recovery").

**Privilege.** Run as a user, `switch`, `back`, `keep`, `prune` and
`recover` re-run their privileged part through `pkexec tam-bundle ...`
(the native adapter ships the polkit action), then run user-session
post-install steps as the user. Run directly as root (for example with
`sudo` from a text console, where no polkit agent runs), they do the
privileged part and print the user-session steps still to run. A home slot
needs no privilege at all.

How it works:

- **When.** A changing command (not a dry run) by a user other than root,
  on a slot that is root's or would be created under a directory of
  root's. Other commands, and dry runs, run as the user.
- **The same command.** `tam-bundle` runs `/usr/bin/pkexec` (an absolute
  path, so `PATH` cannot choose what runs as root) with its own absolute
  path from `/proc/self/exe` and the same arguments. pkexec starts the
  program in `/root` with a cleared environment, so a relative bundle
  directory or `--state` is made absolute first.
- **Session steps.** `user-units-reload`, `qml-cache-purge` and
  `shell-restart` need the user's session. The root run, seeing
  `PKEXEC_UID`, does not run them; it logs `after <step> deferred to uid
  <uid>`. When pkexec returns, the waiting user process reads the log
  lines added since it started, and runs the steps deferred to its own uid,
  once each, recording each result in `session.<uid>`, which the root run
  created for it. Run with `sudo`, the root run creates the same file for
  `SUDO_UID`, so a later `tam-bundle after` by that user is recorded too.
  Any later step that needs no session would run in the root run.
- **Refusals.** pkexec's own exit codes mean the program never ran: `126`
  (authorization refused or dismissed) and `127` (it could not run, as on
  a text console with no polkit agent). Each is reported with what to do
  instead, and nothing changed. A missing pkexec is reported the same way.
- **The polkit action.** `data/org.tamlinux.bundle.policy.in`, installed by
  `make install` with the installed path substituted, names
  `org.tamlinux.bundle`: administrator authentication, kept for a short
  while in an active session. polkit reads actions only from
  `/usr/share/polkit-1/actions`, so the native adapter installs it there; a
  development install elsewhere falls back to pkexec's general action,
  which also asks for an administrator.
- **Tests.** The test build takes `$TAM_BUNDLE_PKEXEC`, a stand-in that
  runs the command as the same user from `/` with a cleared environment
  and `PKEXEC_UID` set; the installed build has no such setting.

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
case. At the start of every changing command (`switch`, `back`, `recover`,
`keep`, `unkeep`, `prune`), under the slot's lock:

| Found | Meaning | Action |
| --- | --- | --- |
| No `switch` file, no `staging.*` | Clean | Continue |
| No `switch` file, a `staging.*` directory | A staging tree that never went live | Remove it, log it, continue |
| `switch` present, `current/BUNDLE` is `from` | Stopped before the exchange | Remove staging, remove `switch`, log "undone"; the old tree is live and untouched |
| `switch` present, `current/BUNDLE` is `to` | Stopped after the exchange | Do steps 6 and 7, log "finished", then report that post-install steps did not run and run them |
| `switch` present, `current/BUNDLE` is neither | Not produced by this program | Stop, change nothing, print what was found |

The exchange keeps the live tree whole, but until recovery the bookkeeping
around it is not: after an interrupted `back`, the tree that was live sits
in `kept/` under the target's name, and the keep list still names the old
"previous". A command that acted on kept trees or the keep list before
recovering could delete the wrong tree (found 2026-11-09: `prune` removed
the tree that had been live, and rollback to it then failed). So:

- **Changing commands recover first.** A dry run changes nothing: it says
  what recovery would do and exits with status 4.
- **Reading commands that use only the live tree** (`check`, `where`,
  `after`) answer from it, since it is always one whole bundle, and print a
  warning naming the interrupted switch. A user often cannot recover a
  root-owned slot, and the answer is correct.
- **`status`** reports the interrupted switch and exits with status 4.

This follows the rule of journaling systems: SQLite rolls back a hot
journal before it reads the database, and ext4 replays its journal at
mount before anything uses the filesystem. Here only the bookkeeping can
be inconsistent, so only the commands that use it must wait for recovery.

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
- Environment settings that put `current/bin` on `PATH` and
  `current/share` on `XDG_DATA_DIRS`. man-db finds `current/share/man`
  from `PATH`, so the manual path needs no setting.
  - `/etc/profile.d/tamlinux.sh`, owned by the package, for login shells.
    A text-console login reads `/etc/profile`, which reads this folder,
    and a graphical session started from that login shell inherits it.
    This is the one file the adapter places under `/etc`, as other
    native packages do.
  - `/usr/lib/environment.d/` for programs the user service manager
    starts.
  - A `user@.service` drop-in under `/usr/lib/systemd/system/` that sets
    `XDG_DATA_DIRS` in the user service manager's own environment. A test
    showed the manager builds its unit search path from its own
    environment only, never from `environment.d`, so without the drop-in
    it would not find `current/share/systemd/user`. The drop-in takes
    effect at the next login.
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

- `user-units-reload` — `systemctl --user daemon-reload` in the invoking
  user's session.
- `qml-cache-purge` — remove the desktop shell's compiled QML cache.
- `shell-restart` — restart the desktop shell if it is running.

Adding a step is a code change to `tam-bundle`, reviewed like any other.

## The manual index

`whatis`, `apropos` and `tam`'s section browsing read man-db's index, not
the pages. The index is built into the bundle, not after install, so it
always matches the pages it describes, rolls back with them and costs a
switch nothing. This replaces an earlier post-install `man-index` step;
no bundle with that step was ever delivered.

- **Where it lives.** `current/share/man` is not one of man-db's system
  hierarchies, so man-db keeps its index inside the hierarchy, as
  `share/man/index.db` (and the same in any locale subdirectory). `man`,
  `whatis` and `apropos` read it there from a read-only tree and write
  nothing, there or in the home directory.
- **How `build` makes it.** With `index man` in the bundle file, after
  merging and before sealing, `build`:
  1. refuses a bundle with no `share/man`, or one where a component
     installed an index of its own;
  2. sets every entry under `share/man` to the newest locked commit's
     time, because the index records each page's modification time;
  3. runs `mandb --create` on the hierarchy (given as an argument, which
     every build of `mandb` reads, unlike `MANPATH`) in a fixed
     environment: no home directory, `--config-file=/dev/null`, `LC_ALL=C`
     and `TZ=UTC`;
  4. removes the empty `cat<section>` directories `mandb` makes for
     formatted pages, and refuses anything else it adds.
- **Recorded and reproducible.** `index.db` is listed and hashed in
  `MANIFEST` like any file, and `BUNDLE` records `tool man-db <version>`.
  The same lock gives the same index bytes whatever the build directory,
  file system or directory order (tested on tmpfs, ext4 and Btrfs with 400
  pages, and with two builds of man-db 2.13.1). The man-db version is not
  part of the components' cache keys, so a man-db update rebuilds no
  component, only the index.
- **Limits.** A man-db that changed its index format would need the bundle
  rebuilt; the format has been `2.5.0` for many years. Running `mandb` as
  root with the live tree on the manual path would rewrite the index, and
  `check` would report it as edited.

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
- **User units.** Settled by test: the adapter's `user@.service` drop-in
  (see "Native adapter package"). If it fails in a real login, the
  fallback is a small fixed set of unit files in the adapter that start
  programs under `current/`.
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
