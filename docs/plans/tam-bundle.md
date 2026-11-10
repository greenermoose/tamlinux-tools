# tam-bundle — design

- **Status:** Revised 2026-11-10 for the developer's requirements of that
  day; awaiting his review. The earlier design (approved 2026-11-09)
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
   switched in at the next login, before the graphical session starts. An
   interrupted switch is finished before any session starts. No reboot.
6. **Dependencies are listed, not carried.** A bundle contains the Tamlinux
   software, a list of the third-party files it expects and where, and a
   recipe for installing the Tamlinux software. It never carries Hyprland,
   Quickshell or their libraries. The rootstock only has to let tam-bundle
   run; tam-bundle checks the list, names what is missing, and the user
   resolves it. A bundle is as portable as that check.
7. **Settings are part of the bundle.** The settings that make up the
   Tamlinux experience are captured and restored with the product. Big
   application state is left out. Secrets are never captured.
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
11. **Fast and cheap.** Nothing to do costs a stat or two at login.
    Preparing a version with nothing to rebuild takes seconds. Disk is
    spent to save CPU and time.
12. **Ours.** C on `libtam`, statically linked, with a man page, `tam`
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
  their host marks.
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

**Placed files** (real files, owned in tam-bundle's record):

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
The allowed roots are a fixed list in tam-bundle's code, not in the bundle,
so a bundle cannot widen them. Nothing is placed in `/etc`, `/usr/local`,
`/opt`, `/var` or a home directory.

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

`/usr/lib/tamlinux` and `/usr/bin` must be on one filesystem (and on
Btrfs, one subvolume), so a staged file can be renamed into place;
prepare checks `st_dev` and refuses otherwise.

**System state, `/var/lib/tamlinux/`** (readable by everyone, nothing
secret in it):

- `bundles/<bundle id>.tbundle` — product bundle files, the portable form;
- `record` — every placed file: path, hash, bundle id;
- `pending` — the armed switch, present between prepare and apply;
- `apply` — the apply journal, present only while an apply runs;
- `host-id` — a random id made once, so a renamed host is still
  recognized;
- `keep`, `log`, `lock`, `stat/<bundle id>` — as before.

The earlier state directory `/var/lib/tam-bundle/` is read once and
folded into this one.

**Per-user state, `~/.local/state/tamlinux/`** (mode `0700`):

- `captures/<capture id>/` — settings captures, readable only by that
  user;
- `staging/`, `pending`, `apply` — the user's half of a switch (see
  "Apply"), on the home filesystem so renames into home stay atomic;
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
recipe/           the bundle file, component recipes and lock it was built from
tree/             the product tree
placed/usr/bin/tam
placed/usr/share/man/man1/tam.1
...
```

A workstation bundle adds a settings capture:

```text
capture/CAPTURE   identity: user, host, host id, date, product it was taken with
capture/FILES     one line per captured file: mark, mode, hash, path
capture/files/... the files, relative to the home directory
capture/machine   locale, keymap and timezone, as values
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
tool tam-bundle 0.6.0
host alex-desktop <host-id>
needs tam-bundle >= 0.6.0
needs file /usr/bin/Hyprland exact sha256:<64 hex>  arch=hyprland
needs file /usr/bin/quickshell >= 0.3.0            arch=quickshell void=quickshell
needs file /usr/lib/libdbus-1.so.3                 arch=dbus void=dbus
needs file /usr/bin/python3 >= 3.12                arch=python void=python3
after user-units-reload
after man-index
built 2026-11-10T18:00:00Z
```

**`MANIFEST`** lists both parts, sorted, one line per entry; the first
field says which part:

```text
format 2
t d 0755 -               lib
t f 0644 sha256:<64 hex> share/fonts/...
p f 0755 sha256:<64 hex> usr/bin/tam
p f 0644 sha256:<64 hex> usr/share/man/man1/tam.1
```

`t` is the product tree, `p` placed. Placed entries are files and
directories only, never links. Tree entries may be relative links that
stay inside the tree, as before.

**The bundle hash** is SHA-256 over `BUNDLE` without `hash` and `built`,
then `MANIFEST`, then `SETTINGS`. A capture's hash covers `CAPTURE`,
`FILES` and `machine`.

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
- **Exact:** `exact sha256:<hash>` where only the very build will do. The
  main case is Hyprland: the `fred.*` title-bar plugins are compiled against
  one Hyprland build, so the bundle records the hash of the `Hyprland` it
  was built against.
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
  script runs) stays the recipe's job; the `tam` and shell test suites are
  what catch omissions there.
- **When it is checked:** at prepare, before anything changes, and again by
  `tam-bundle needs <bundle>` at any time. Missing entries are listed all
  at once, with their hints.

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
- **Never:** secrets. Some paths are refused outright by tam-bundle's code
  (`.ssh/`, `.gnupg/`, `.local/share/keyrings/`, agent credential folders
  such as `.gemini/`, `.codex/` and `.claude/` credentials,
  `.config/gh/hosts.yml`). Every other captured file is scanned for secret
  patterns (private key blocks, GitHub, OpenAI, Anthropic, Slack and AWS
  token prefixes, `password=` and `token=` assignments); a match is left
  out, and the capture names the file and the line number, never the
  value.

**Machine settings.** From `/etc` a capture takes only three values:
locale (`/etc/locale.conf`), keyboard layout (`/etc/vconsole.conf` and its
X layout) and timezone (the zone `/etc/localtime` points to). They are
`common`. Apply sets them through the rootstock's own tools
(`localectl`, `timedatectl` on systemd), after checking each value exists
on the target. Never captured: `fstab`, `crypttab`, hostname, machine-id,
boot loader settings, saved Wi-Fi passwords, users and passwords.

**Capturing while programs write.** Each file is read, hashed and then
`fstat`ed again; if its size or change time moved during the read, it is
read again, up to three times, then reported. A file a program rewrites
by itself (an editor's `settings.json`) is captured as it is at that
moment.

**Host marks.** On the host a capture was made on, everything applies. On
another host:

- `common` files apply;
- for `host` files the target keeps its own; where it has none, it gets
  the cultivar's default from the product tree, if there is one;
- the machine values apply (they are `common`);
- tam-bundle lists every file it skipped.

The unit is a whole file. When a `common` file names the capturing host or
one of its display outputs (`DP-1`, `HDMI-A-1`), capture warns that the
file looks mixed, so the user can split it once and mark the parts.

**Edits relative to the product.** `FILES` records, for each captured
file, whether it equals the cultivar default of that product version or
was edited, so a capture never pretends an edited file came unchanged from
a source commit.

## Prepare and apply

A switch has two halves. **Prepare** does all the slow work while the user
keeps working. **Apply** does the few renames that make it live, when no
graphical session is running. Each half has a system part (root) and a
settings part (the user).

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
5. **Stage, system part (root, through `pkexec`).** Unpack the product
   tree into `version/<id>/` (on Btrfs, clone unchanged files from the
   current version), copy placed files into `staging/<id>/`, hash every
   copy, then `syncfs` once. Write `pending` (from, to, the list of placed
   changes) atomically and sync. The old version stays unpacked and whole.
6. **Stage, settings part (the user).** First capture the current settings
   automatically (so nothing is lost and the switch can be undone by
   rolling forward again). Then copy the files that will apply into
   `~/.local/state/tamlinux/staging/`, sync, and write the user's
   `pending`.
7. **Tell the user**, warmly:

   ```text
   Tamlinux 0.7.0 is ready.

   We don't have to restart the whole computer. Just log out, and we'll
   get the house in order for you. When you log in again, you'll be
   amazed!
   ```

Preparing again replaces an earlier pending switch. `tam-bundle cancel`
removes it; nothing live has changed.

### Apply (at the next login, or at boot)

**System part, `tam-bundle apply` (root).** Run by
`tam-bundle-apply.service`, a oneshot system unit from the bootstrap
package, started in two ways:

- by the session entry (`tam-session`), before it starts the compositor;
  a polkit rule from the bootstrap package lets a user in an active local
  session start this one unit without a password. Starting it grants
  nothing new: it only applies what an authorized prepare already staged
  and armed;
- at boot, ordered `Before=systemd-user-sessions.service`, so no one can
  log in until an interrupted apply is finished.

Steps, under the lock:

1. Nothing pending and no journal: exit at once (a stat or two).
2. **Busy check.** If any graphical Tamlinux session is running, or any
   process still runs a program from the old version's tree (`/proc/<pid>/
   exe` and `maps`), leave the switch armed, say which, and exit. The
   session entry then starts the old version as it is; nothing is mixed.
3. Write the journal `apply` (copied from `pending`) and sync.
4. For each placed change: move the old file into the old version's
   `.placed/`, rename the staged file into place (`rename(2)`, atomic per
   file); remove placed files the new version no longer has the same way.
   `fsync` each changed directory.
5. Replace `current` with a new link (`symlink` beside it, then `rename`
   over it) and `fsync` `/usr/lib/tamlinux`.
6. Update `record`, `keep` (the old id becomes "previous"), `stat`, `log`;
   set the machine values; `syncfs`; remove `apply` and `pending`.
7. Run the system post-install steps (`system-daemon-reload`, `man-index`,
   `sysctl-apply`, `tmpfiles-create`) and log each. Settings that only take
   effect at boot (logind, the system manager) are named in the message.

**Settings part, `tam-bundle apply-settings` (the user).** Run by the
session entry right after the system part, before the compositor. It only
proceeds when the system part has finished for the product version the
staged settings belong to; the same journal pattern: write the journal,
rename each staged file into place, sync, remove the journal. It never
runs as root and never follows links out of the home directory.

**Interruption.** Apply only moves files that prepare already wrote and
synced, so recovery always goes forward: rerunning apply reads the
journal, and for each step checks whether it is already done (the
destination already has the new hash) or still to do (the staged file is
still there). The boot ordering and the session entry both rerun it before
any session starts. An apply that finds a state it did not produce (a
destination with neither hash) stops, changes nothing more, and says what
it found; the session entry then refuses to start the graphical session
and says how to recover from a text console.

**What the user sees.** At login, after a pending switch:

```text
Getting the house in order: Tamlinux 0.6.21 -> 0.7.0 ... done.
Welcome back!
```

With nothing pending, nothing is printed and nothing measurable is spent.

## Restore and rollback

- `tam-bundle back` prepares the previous product version and the
  settings capture taken with it; `tam-bundle restore <bundle>` does the
  same for any retained bundle, forward or backward.
- Both always capture the current settings first, automatically.
- `--product-only` keeps the current settings with the other software;
  `--settings-only` restores a capture onto the current software. When
  the capture was taken with another product version, tam-bundle warns
  before preparing.
- A restore makes the listed settings paths match the capture: files the
  capture lacks are removed from those paths (they are in the automatic
  capture). Paths not on the list are never touched.
- Product rollback to the previous version reads its unpacked tree and
  `.placed/`; anything older is unpacked from its bundle file first.

## Login and boot files

Some placed files can stop the machine from booting or the user from
logging in. tam-bundle's code, not the bundle, lists them:

- system units and drop-ins (`/usr/lib/systemd/system*`,
  `logind.conf.d`, `system.conf.d`, `system-sleep`), `sysctl.d`,
  `tmpfiles.d`, `zram-generator.conf.d`;
- polkit actions and rules; the Wayland session entry; the session
  command `/usr/bin/tam-session`;
- anything under `udev`, `modules-load.d` or `modprobe.d`.

A change to any of them is named at prepare and needs the user's approval
of that exact list. The previous version stays unpacked as the rollback
point, and `sudo tam-bundle back` followed by `sudo tam-bundle apply`
works from a text console with no graphical session and no polkit agent.

## Command names

Every command Tamlinux places in `/usr/bin` is `tam` or `tam-*`; `build`
refuses any other name. That makes a collision with a distribution package
very unlikely, and the ownership check catches the rest. The current
exceptions are renamed: `suspend-unless-stay-awake` becomes a `tam-*`
command.

`hi` and `start` must keep working on every Tamlinux workstation, each
leading to a `tam-*` command. How they are provided and how that is
enforced is still open.

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
| `tam-bundle capture [-o <file>]` | Capture the user's settings; with `-o`, write a workstation bundle | No |
| `tam-bundle install <bundle>` | Prepare a product or workstation bundle | Prepare's system part |
| `tam-bundle back`, `restore <bundle>` | Prepare the previous or a named bundle, with `--product-only` or `--settings-only` | Prepare's system part |
| `tam-bundle cancel` | Drop a pending switch | System part |
| `tam-bundle apply` | System part of a pending switch; run by the unit | Yes |
| `tam-bundle apply-settings` | Settings part; run by the session entry | No |
| `tam-bundle status` | Current, previous, pending, interrupted, space used | No |
| `tam-bundle check [--full]`, `where <path>` | Edited, missing or extra files; owner of one file | No |
| `tam-bundle keep`, `unkeep`, `prune [--yes]` | Protect, list and remove retained bundles | Yes |
| `tam-bundle after` | Rerun the user-session post-install steps | No |

Privilege works as before: run as a user, the root parts re-run through
`/usr/bin/pkexec` with `/proc/self/exe`; run as root from a text console,
they do the root part and print what the user still has to run. Refusals
(`126`, `127`) are explained. `tam-deploy` keeps the stage logic and calls
these commands.

## Retention

- **Always kept:** current, previous, the recovery anchor and anything
  pinned with `keep`.
- **Unpacked:** current and previous only.
- **Removed only on request:** `tam-bundle prune` lists what it would
  remove and the space it would free, and removes only with `--yes`. A
  plain prune keeps the five most recent product bundles and the five most
  recent captures beyond the always-kept set. Nothing runs on a timer.
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

## Bootstrap

The rootstock provides one small native package, `tamlinux-bootstrap`
(Arch first; the same idea built with `xbps-src` on Void):

- `/usr/bin/tam-bundle`, built static, so it needs no library and runs on
  glibc and musl rootstocks alike;
- its manual page, its polkit action and the polkit rule for the apply
  unit;
- `tam-bundle-apply.service` (on a systemd rootstock; Void's runit needs
  its own trigger, designed with the Void port);
- the empty `/usr/lib/tamlinux/` and `/var/lib/tamlinux/`;
- a dependency on polkit.

tam-bundle is upgraded only through that package, never by a bundle: the
ownership check would refuse a bundle that tries to overwrite a
package-owned file anyway. A newer tam-bundle reads every older bundle and
capture format; an older one refuses a newer format by name.

**Static linking and users.** A static glibc program cannot use NSS
modules, so it cannot look up users that come from `systemd-homed`, LDAP or
similar. tam-bundle needs a user's home directory only for the settings
part, which runs as that user and takes `$HOME`; the root part needs only
the numeric id from `PKEXEC_UID` or `SUDO_UID`. To verify when building:
static against musl, or static glibc with no NSS lookups.

## From the earlier design

**Stays:**

- lock file, recipes, `build`, build cache, fixed build paths in user and
  mount namespaces, reproducibility test;
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
  `needs`; what remains becomes `tamlinux-bootstrap`.
- **State** moves from `/var/lib/tam-bundle/` to `/var/lib/tamlinux/`.
- **Host:** `host any` / `host <name>` refusal becomes the host record
  and host marks.
- **Personal configuration** is now captured (it was excluded).

**Goes:**

- `renameat2(RENAME_EXCHANGE)` of whole trees, and the recovery table
  built on it;
- `current/bin` on `PATH` and `current/share` on `XDG_DATA_DIRS`:
  `/etc/profile.d/tamlinux.sh`, `/usr/lib/environment.d/50-tamlinux.conf`
  and the `user@.service.d/20-tamlinux-bundle.conf` drop-in are removed;
  user units are placed in `/usr/lib/systemd/user/` instead.

## Proofs required

1. **Build and identity:** the same lock builds the same product hash
   twice; two captures of one version have different ids; the build-time
   link check refuses an unlisted library.
2. **Capture and restore:** change real settings, capture, then restore on
   a fresh user without consulting the original home or any Git
   repository; the secret scan leaves out planted test tokens.
3. **Second workstation:** install with only the rootstock, the bootstrap
   package and the bundle file. A missing dependency is named before
   anything changes; once supplied, the experience matches, host files
   skipped and listed.
4. **Forward and backward:** restore nonadjacent versions and different
   captures; verify placed files, removals, settings, machine values and
   post-install steps.
5. **Interruption:** a fault point after every apply step and every
   hundredth file, for both parts, and `SIGKILL` at random moments; after
   each, the next apply finishes forward and `check --full` shows exactly
   the new version. A power cut on real hardware during apply, at least
   once, before the daily cutover.
6. **Ownership:** a placed path owned by a rootstock package, and an
   unowned file at a placed path, are both refused before any change;
   files in `/usr/local`, an extra native package and a user's own
   `~/.local/bin` are untouched after install, restore and prune.
7. **Login:** the session entry with nothing pending adds no measurable
   delay; with a pending switch it applies, then starts the new version;
   with a busy old process it starts the old version and leaves the switch
   armed.

## Migration order

Each step lands only on the developer's approval, one at a time, with a
rollback point. Nix and Home Manager stay the daily delivery until the
proofs pass.

1. **Design review.** This document.
2. **Tool.** `tam-bundle` 0.6.0: the format 2 bundle and capture, prepare
   and apply, ownership and dependency checks, in temporary roots with the
   crash tests. Built static.
3. **Recipes.** `tamlinux-packages` builds with `PREFIX=/usr`, the
   placed/tree split and `needs` lines; components that install outside
   the allowed roots are fixed.
4. **Bootstrap package.** `tamlinux-bootstrap` replaces `tamlinux-native`;
   the dormant slot is converted to `version/<id>/` plus `current`.
5. **Second workstation.** Proofs 2 to 6 on a second machine first.
6. **Cutover on the daily workstation.** Home Manager's Tamlinux commands
   leave `~/.local/bin` (the shadow warning confirms none remain) and the
   Nix profile folders leave `PATH`.
7. **`tam-deploy` back end, recovery anchor, then Nix removal,** each
   separately approved.

## Open

- **`hi` and `start`:** how they are provided on every Tamlinux
  workstation, leading to `tam-*` commands, and how that is enforced.
- **The polkit rule for the apply unit:** confirm on Arch that an active
  local session can start exactly one unit without a password, and that a
  text-console login counts as active and local.
- **Busy check limits:** which long-lived processes (terminal
  multiplexers, agents left running on a text console) hold old files, and
  whether apply should wait briefly before leaving the switch armed.
- **The settings list's first contents:** written from the files Home
  Manager links today, with marks, before the second-workstation proof.
- **Void:** the apply trigger under runit, and `xbps-query` for ownership
  and versions.
