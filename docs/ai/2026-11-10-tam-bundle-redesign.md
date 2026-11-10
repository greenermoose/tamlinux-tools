# Revised design for `tam-bundle`

- **Date:** 2026-11-10
- **Stage:** Plan: revision of the design document before any code.
- **CLI Tool:** Claude Code 2.1.322
- **Model:** Claude Opus 5.5 (`claude-opus-5-5`)
- **Transcript:** Retained privately by the author.
- **Commits:** the commit that adds this record.
- **Authorship:**
  - Fred set the requirements: real commands in `/usr/bin`, the product
    bulk under `/usr/lib/tamlinux` behind one `current` link, a plain
    PATH, bundles that list dependencies instead of carrying them,
    settings captured into bundles with host awareness, and the answers to
    ten design questions (switch at next login, dependency list by path,
    settings tiers, host marks, restore, files outside home, command
    names, development builds, retention, bootstrap).
  - Claude proposed the options he chose from and wrote the revised
    design.
  - Fred has not yet reviewed the revised document.
- **Checked against:**
  - The current `tamlinux-native` file list and the commands in the
    dormant product tree (241 of 244 named `tam*`).
  - Mount layout: `/usr` and `/var/lib` on one Btrfs subvolume, `/home`
    on another.
  - Arch's man-db (`man-db.timer`; no install hook) and the polkit rules
    directory.

## Guiding prompts

> I'm refining the requirements for tam-bundle. [...] I don't care about
> symlinks for things like atomically switching directories, but I do
> care about putting things in the correct place. I want the binaries for
> Tamlinux to be in /usr/bin, not hidden away under /usr/lib/tamlinux/.

> I do not want symlinks in /usr/bin. I want the actual binaries there.
> And I want a reasonable PATH that doesn't have a bunch of kooky folders
> on it.

> Inside the bundle is the Tamlinux software, a list of its dependencies,
> and a recipe for installing the Tamlinux software.

> I want to follow the standard Linux file hierarchy rules that have been
> around for decades.

> Get it done. I'm going to bed now. Do as much as you can before the
> system suspends. Good luck!

## Key decisions

- **Placed files and a product tree:** a build installs with
  `PREFIX=/usr`; `usr/lib/tamlinux/current/` becomes the product tree,
  everything else must fall under a fixed list of standard roots.
- **Prepare and apply:** slow work while the user works; renames at the
  next login through a oneshot system unit started by the session entry
  and at boot before user sessions; recovery always goes forward.
- **System and settings halves:** root never writes into a home
  directory; the user's half runs as the user before the compositor.
- **Manual index:** pages now live in `/usr/share/man`, so the index
  built into the bundle gives way to the rootstock's man-db.

## Verification

- Documentation only; no code changed.
- Open items needing tests are listed in the design: the polkit rule for
  the apply unit, the busy check, static linking and user lookups.
