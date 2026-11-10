# 2026-11-09 — File bundles, install records and atomic switching

- **Question:** What should `tam-bundle` learn from existing tools before
  it is designed?
- **`tam-bundle`:** a planned C command on `libtam` that installs a
  named, versioned set of real files, records them, and switches between
  versions with rollback.
- **Status:** Answered for its date. All findings are proposals; none is
  approved work.
- **Search date:** 2026-11-09.
- **Scope:** a focused survey of four projects named in advance (OSTree,
  pacman/libalpm, yadm, GNU Stow), a review of Nix's ideas, and adjacent
  projects found along the way (systemd-sysext, chezmoi, GNU Guix,
  GoboLinux, Homebrew).

## Question and constraints

`tam-bundle` replaces Nix and Home Manager as the way Tamlinux delivers its
own files. The constraints it is measured against:

- **Real files in final folders:** no symlink chains into a store.
- **One bundle, one identity:** a name, a version, the exact source
  commits, and a content hash.
- **A readable record:** for every installed file, which bundle and hash
  it came from.
- **Atomic switch and one-command rollback:** never a half-installed state.
- **No background process:** no daemon, socket, timer or polling, and no
  network unless asked.
- **Fast:** unchanged files are not rewritten.
- **Portable:** works the same on Arch and Void. Third-party software
  stays with the native package manager.
- **Ours:** C on `libtam`, linked statically.

## Search paths

- **Primary documentation:**
  - OSTree: <https://ostreedev.github.io/ostree/> (introduction,
    repository, deployment and atomic-upgrade pages).
  - yadm: <https://yadm.io/docs/> (getting started, alternates,
    bootstrap, FAQ) and <https://github.com/yadm-dev/yadm>.
  - systemd-sysext: the `systemd-sysext(8)` page for systemd 262.
- **Source:** pacman `v7.1.0`, `lib/libalpm/add.c`, read from a local
  clone of <https://gitlab.archlinux.org/pacman/pacman>. The GitLab web
  view refuses automated fetches.
- **Kernel interface:** `rename(2)`, as installed with the man pages on an
  Arch system.
- **Searches:** release status of OSTree, yadm and GNU Stow; pacman
  interrupted-upgrade reports.

## Candidates

### OSTree (inspiration; no code copied)

- **What it is:** a content-addressed repository of files, at
  `/ostree/repo`. Objects are addressed by SHA-256.
- **How it installs:** each deployment is "checked out as hard links into
  the repository", so every version is deduplicated and an upgrade "only
  costs disk space proportional to the new files" (introduction page).
- **The atomic step is a symlink swap.**
  - `/boot` points at `/ostree/boot.0` or `/ostree/boot.1`, and an
    upgrade builds the new one beside it, then "atomically swap[s] the
    symbolic link" (atomic-upgrades page).
  - The running system is untouched until the next boot. A switch takes
    effect at reboot.
- **`/etc` and `/var`:**
  - `/etc` gets a three-way merge: old defaults, the booted `/etc` and
    new defaults. Locally modified files are kept; unmodified files follow
    the new defaults.
  - `/var` is shared between deployments and never touched.
- **Current release:** 2026.4, packaged in Arch `extra-staging` in
  October 2026.
- **License:** GitHub reports no single SPDX identifier for the
  repository; check the source tree before adapting any code.

**What carries over:**
- Build the new version entirely beside the old one, then make one
  atomic step.
- Content-address files by SHA-256, so identical files are stored once.
- Keep the user's changes to defaults on upgrade (the `/etc` merge rule
  maps onto `tam-bundle`'s rule for files a user has edited).

**What does not:**
- A whole-OS image with reboot-to-switch is far heavier than needed.
- Its atomic step is a symlink, which the "real files" constraint rules
  out.
- Hardlinked files are shared with the repository, so the files must be
  treated as read-only. An edit in place would corrupt every version.

### pacman / libalpm 7.1 (inspiration and a warning; GPL-2.0)

Read from `lib/libalpm/add.c` at tag `v7.1.0`:

- **Ordinary files are written straight over the old file.**
  `perform_extraction` uses libarchive with `ARCHIVE_EXTRACT_UNLINK`,
  which removes the old file and writes the new one at the final path.
  It does not set libarchive's safe-write option, so no temporary name
  and rename is used for ordinary files.
- **Protected config files** (`backup=` in a package) get the careful
  path.
  - The new copy is written as `<file>.pacnew`.
  - MD5 hashes of the original, current and new versions decide whether
    to `rename()` it into place or leave the `.pacnew` for the user.
- **The package is recorded as installed only after all its files are
  extracted.** `_alpm_local_db_write` runs at the end of
  `commit_single_pkg`.
- **`add.c` contains no `fsync`,** and a package whose extraction fails
  partway is not rolled back.
- **Field reports agree:** an interrupted upgrade can leave half-installed
  packages, and recovery is reinstalling what was being upgraded.
  These are Arch, Manjaro, EndeavourOS and Artix forum threads, so they
  are secondary evidence.

**What carries over:**
- Record after the files are in place, never before.
- Treat files the user may have edited as a separate class, with a hash
  comparison deciding what happens to them.
- Conflict checks before the first write: refuse when another owner's
  file is in the way.

**What to do better:**
- Write each new file under a temporary name in the same directory, then
  `rename()` it into place.
- `fsync` before the step that makes a version current.
- Use SHA-256 rather than MD5 for file identity.

### yadm (inspiration for the home-directory side; GPL-3.0)

- **What it is:** git with the home directory as the work tree. The
  repository lives at `~/.local/share/yadm/repo.git`.
- **Untracked files are hidden by default:** `status.showUntrackedFiles`
  is set (FAQ).
- **Per-host files ("alternates"):** files carry suffixes such as
  `##hostname.<name>` or `##os.Linux`. yadm "will automatically create a
  symbolic link to the appropriate version" (alternates page), so
  alternates are symlinks by default. Templates produce generated files.
- **Bootstrap:** runs a user script after a clone; the docs ask for it to
  be idempotent.
- **Safety:** no guard against `git clean` or `git reset --hard` on the
  home work tree is documented on the pages read.
- **Version:** the latest tag is `3.5.0`; GitHub lists no release objects.

**What carries over:**
- The bare-repository, home-as-work-tree layout.
- Hide untracked files by default.

**What not to copy:**
- Symlinked alternates. Host-specific settings are better handled by the
  program's own include mechanism (a per-host file included by hostname),
  so every file stays real.

**A gap we must close ourselves:** a wrapper that does not expose
destructive commands. yadm does not provide one.

### GNU Stow (rejected as a model; GPL-3.0)

- **What it is:** a symlink farm manager. Version 2.4.0 (April 2024) was
  the first release since 2019.
- **Why it doesn't fit:** it is the same structure as Home Manager's file
  links, which `tam-bundle` exists to replace.
- **Lesson:** a symlink installer turns "where does this file come from?"
  into a chase. Stow's conflict check (refuse when a real file is in the
  way) is the only part worth keeping, and pacman already shows it.

### Adjacent projects

- **systemd-sysext (rejected as the primary mechanism):**
  - It overlays extension trees onto `/usr` and `/opt` with read-only
    overlayfs.
  - The manual says extensions "should not be misunderstood as a generic
    software packaging framework, as no dependency scheme is available".
  - On refresh there is "a brief moment where neither the old nor the new
    overlayfs file system is mounted".
  - It targets immutable OS images, not real files that a user can read
    in place.
- **chezmoi (rejected for this use):** it keeps a source directory and
  copies files into home. Two copies is the drift problem this work
  avoids.
- **GNU Guix (rejected):** the same store-and-profile-symlink model as
  Nix.
- **GoboLinux and Homebrew (rejected):** both install each version in its
  own directory and expose it through symlinks.

## Review of Nix's ideas

| Nix idea | Verdict | Reason |
| --- | --- | --- |
| Content addressing (hash of the contents names the thing) | Keep | SHA-256 per file, plus one hash over the bundle's file list. Identical sources give the identical hash, and verification is exact. |
| Exact pins and a lock file | Keep | A plain-text lock file of exact commits per component. It is what makes a version reproducible and reviewable. |
| Generations and an atomic profile switch | Keep the idea, change the mechanism | Keep previous versions and switch in one step, but with real directories and `renameat2(RENAME_EXCHANGE)` instead of a profile symlink. |
| Rollback | Keep | One command back to the previous bundle, with the previous bundle's files still on disk. |
| GC roots and garbage collection | Keep, simplified | A short, explicit list of what to keep (current, previous, the recovery anchor). Everything else is removable after a dry run. No reference scanning. |
| Declarative description of the result | Keep, smaller | A bundle recipe lists files and their destinations. There is no evaluation language: the description is data, not a program. |
| Closure completeness (every dependency inside the store) | Drop | Third-party dependencies come from the native package manager. A Tamlinux bundle declares which native packages it needs and checks they are present, instead of carrying them. This also removes the graphics-driver wrappers that Nix-built GUI programs need outside NixOS. |
| Sandboxed, reproducible builds | Partly keep | Build from exact commits in a clean directory, and record compiler and dependency versions. A full build sandbox is out of scope. |
| Binary substitution (download prebuilt outputs from a cache server) | Drop | Bundles are built locally or copied from the build host. No server, no network unless asked. |
| Multi-user store with a build daemon | Drop | It is the source of the daemon and socket units. `tam-bundle` runs as the invoking user, with root only for system paths through `pkexec`. |
| `/nix/store` paths as the install location | Drop | Files land at their real paths. The record, not the path, says which bundle owns a file. |

## Comparison

| Need | OSTree | pacman | yadm | Stow | Nix | Proposed for `tam-bundle` |
| --- | --- | --- | --- | --- | --- | --- |
| Real files at final path | Hardlinks | Yes | Yes (except alternates) | No (symlinks) | No (symlinks) | Yes |
| Atomic switch | Symlink swap at boot | No | n/a | No | Symlink swap | Directory exchange |
| Rollback | Yes | Manual downgrade | git | Manual | Yes | Yes |
| Survives interruption | Yes | Partly | git's own | No | Yes | Required (tested) |
| Content hash | SHA-256 | MD5 for config files | git SHA-1/256 | No | Yes | SHA-256 |
| Needs a daemon | No (optional service) | No | No | No | Usually | No |
| Distribution-independent | Yes | Arch family | Yes | Yes | Yes | Yes |

## Candidate design elements (proposed)

These carry into the design document as proposals for review, not
approvals:

1. **Staging beside the live tree.** A switch builds the new version in a
   staging directory on the same filesystem, with each file written to a
   temporary name, `fsync`ed and renamed.
2. **One exchange.** `renameat2(RENAME_EXCHANGE)` swaps the staging
   directory with the live Tamlinux directory in one step. The old tree
   lands where staging was and becomes the rollback copy.
   - `rename(2)` documents the flag for any two existing paths, including
     non-empty directories.
   - Filesystem support must be verified on Btrfs and ext4 by a test,
     before relying on it.
3. **Cheap copies.** On Btrfs, unchanged files are cloned (reflinked)
   from the current tree into staging instead of copied, so keeping
   versions costs disk only for files that changed. Unlike OSTree's
   hardlinks, a reflink copy is independent, so an edit in place cannot
   corrupt another version.
4. **Record last.** The plain-text record (path, SHA-256, bundle, version)
   is written and synced after the exchange, as pacman records after
   extraction. On start, a leftover staging directory or a missing record
   means the last run was interrupted. The tool then reports and
   finishes, or undoes, before doing anything else.
5. **User-edited files.** Files outside the exchanged directories (user
   units, anything under `/etc`) follow pacman's three-hash rule:
   - Replace a file the user has not changed.
   - Keep a changed one and write the new one beside it as `.tamnew`.
   - Back up a file the tool did not install before replacing it.
6. **Native dependencies are declared and checked, not installed.**
7. **Crash test:** the test suite kills the tool at each step of a switch
   and checks that the result is either the old version or the new one,
   never a mix.

Doing nothing was considered: continuing with Nix and Home Manager is the
current state, and keeps the symlinks, daemon units and store this work
exists to remove.

## Verification

- Proposal 2 is verified by a small C test of `renameat2(RENAME_EXCHANGE)`
  on non-empty directories on Btrfs and ext4.
- Proposal 3 is verified by measuring disk use before and after a no-change
  switch.
- Proposals 4 and 7 are verified by the interruption test.
- The whole design is verified when an older bundle can be switched back to
  after several newer ones.

## Licenses and attribution

- No code is copied from any candidate. The design takes general ideas
  from OSTree, pacman, yadm and Nix, credited here.
- Before adapting any source, record the exact version and license in
  `UPSTREAM.md`.
  - pacman is GPL-2.0-or-later (confirmed in the `add.c` header).
  - yadm is GPL-3.0 (its GitHub README).
  - GNU Stow is GPL-3.0 (not checked at source in this survey).

## Records

- Earlier text: `git log -- upstream/2026-11-09-file-bundles-and-atomic-switch.md`
