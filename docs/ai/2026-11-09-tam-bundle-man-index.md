# `tam-bundle` 0.4.0: the manual index, built into the bundle

- **Date:** 2026-11-09
- **Stage:** Develop: the manual index for `tam-bundle`, per the approved
  [design](../plans/tam-bundle.md), changed as recorded here.
- **CLI Tool:** Claude Code 2.1.321
- **Model:** Claude Opus 5.5 (`claude-opus-5-5`)
- **Transcript:** Retained privately by the author.
- **Commits:** the commit that adds this record.
- **Authorship:**
  - Fred asked for the work and chose the approach.
  - Claude ran the trials, wrote the code, the tests, the README and design
    updates, and this record.

## Guiding prompts

> Continue plan 53 step 4: the man-index step

After trials, four options were offered: build the index into the bundle;
a post-install step writing it into the live tree; a post-install step
writing it outside the tree under `/var/cache/man`; or no index. Fred:

> Go with option 1

## Trials before the choice

- For a hierarchy outside man-db's system paths, `mandb` writes
  `index.db` inside the hierarchy.
- With page times fixed, the index was byte-identical across runs and
  directories, and on tmpfs, ext4 and Btrfs for 400 real pages copied in
  shuffled order.
- On a read-only tree whose pages were newer than the index, `whatis`,
  `apropos`, `man` and `man -w` worked as a user and wrote nothing.

## What changed

- **New `src/tam-bundle/manindex.c`:** with `index man` in the bundle
  file, `build` refuses a bundle without `share/man` or with a
  component's own index, sets every entry under `share/man` to the newest
  locked commit's time, runs `mandb --create` on the hierarchy in a fixed
  environment, removes the empty `cat<section>` directories it makes, and
  refuses anything else it adds. `BUNDLE` records `tool man-db <version>`;
  the components' cache keys do not include it.
- **`man-index` is no longer a post-install step;** naming it is refused.
  No bundle with that step was delivered.
- **Found on the way:** one build of `mandb` ignores `MANPATH` and indexed
  `/usr/local/man` instead, so the hierarchy is passed as an argument,
  which both builds on the workstation read; both gave the same bytes.
- **Version:** 0.4.0.

## Verification

- `make test` passes, and both bundle test scripts passed on tmpfs, Btrfs
  and ext4.
- New tests in `tests/tam-bundle/build.sh`, with a small repository of two
  pages and a link:
  - no index without `index man`; adding it rebuilds no component;
  - `index.db` hashed in `MANIFEST`, `tool man-db` in `BUNDLE`, only
    `index.db` added, and the newest commit's time in the index;
  - the same index and hash from an empty cache;
  - after a switch, on the read-only live tree, `whatis` finds the pages
    and the link and `apropos -s 5` lists the section; `check --full`
    still matches and nothing is written in the home directory;
  - rollback takes the index away with its pages;
  - refusals: no `share/man`, a component's own index, `index info`, and
    the old `after man-index`.
