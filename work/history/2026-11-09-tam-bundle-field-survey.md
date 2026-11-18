# Field survey for `tam-bundle`

- **Date:** 2026-11-09
- **Stage:** Plan: a focused field survey before designing `tam-bundle`,
  a planned C command that installs, records and switches versioned sets
  of real files.
- **CLI Tool:** Claude Code 2.1.321
- **Model:** Claude Opus 5.5 (`claude-opus-5-5`)
- **Transcript:** Retained privately by the author.
- **Commits:** the commit that adds this record.
- **Authorship:**
  - Fred chose the direction: Tamlinux's own install record instead of
    native packages, the command name, this repository as its home, and
    the focused survey.
  - Claude researched and wrote the survey record and `UPSTREAM.md`.
  - Fred reviewed the survey without edits.
- **Checked against:**
  - OSTree's introduction, repository, deployment and atomic-upgrade
    documentation.
  - pacman `v7.1.0` `lib/libalpm/add.c`, from a local clone.
  - yadm's documentation and GitHub repository.
  - The systemd 262 `systemd-sysext(8)` page.
  - `rename(2)`.

## Guiding prompts

> Think more about whether tamnix should produce pacman packages or keep its
> own record. What do you feel would be serve our needs over the long haul
> and why?

> Yes, close that question. tamnix will keep its own record.

> tam-bundle is fine

> yes, tamlinux-tools

> yes, run the focused survey

> Looks great. No edits from me.

## Key decisions

- **Its own record:** `tam-bundle` keeps its own install record, the same
  on every distribution. A small native adapter package per distribution
  carries `tam-bundle` itself and declares third-party dependencies.
- **The survey's seven design elements are proposals for the design
  document:**
  1. staging beside the live tree
  2. one `renameat2(RENAME_EXCHANGE)`
  3. reflinked unchanged files
  4. the record written last
  5. three-hash handling of user-edited files
  6. native dependencies checked, not installed
  7. an interruption test
- **This repository** gains `UPSTREAM.md` and `upstream/`, as the field
  survey guide expects of every Tamlinux repository.

## Verification

- Documentation only; no code changed.
- pacman claims were read from `add.c` at tag `v7.1.0`. The GitLab web
  view refused automated fetches, so a local clone was used.
- Unverified claims are marked in the record, for example GNU Stow's
  license and filesystem support for `RENAME_EXCHANGE`.
