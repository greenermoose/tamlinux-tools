# `tam-bundle` 0.2.0: `build` and `lock`

- **Date:** 2026-11-09
- **Stage:** Develop: the second slice of `tam-bundle`, per the approved
  [design](../plans/tam-bundle.md).
- **CLI Tool:** Claude Code 2.1.321
- **Model:** Claude Opus 5.5 (`claude-opus-5-5`)
- **Transcript:** Retained privately by the author.
- **Commits:** the commit that adds this record.
- **Authorship:**
  - Fred asked for the next part of the work.
  - Claude wrote the code, the tests, the README and design updates, and
    this record.

## Guiding prompts

> Continue plan 53 step 4: build and lock for tam-bundle.

After a trial found build paths in the built programs, and three choices
were offered (fix each component, fixed build paths, or both):

> Option 3, both

## What changed

- **New files in `src/tam-bundle/`:**
  - `lock.c`: reading lock files, and `lock`, which rewrites one line and
    keeps the rest of the file;
  - `recipe.c`: the bundle file and recipes, their checks, and the build
    order from `after-build`;
  - `build.c`: the build cache and its keys, running `make` in a fixed
    environment with a log, the check that nothing lands outside the
    prefix, merging the outputs, and sealing;
  - `git.c`: finding, fetching and exporting exact commits through `git`;
  - `isolate.c`: each `make` in its own user and mount namespace, with the
    work tree and build-only dependencies (read-only) at fixed paths
    under `/tmp/tam-bundle`;
  - `seal.c`: sealing, moved out of `main.c` so `seal` and `build` share
    it.
- **`slot.c`:** tree removal now works under any directory, not only a
  slot.
- **Additions to the design, recorded in it:**
  - `recipes/bundle` holds the bundle-wide name, host, needs and steps; the
    version comes from `build --version`;
  - `output build-only` and `@<component>@` for build-only components'
    private prefixes;
  - tam-bundle sets `PREFIX` to `<slot>/current` and refuses files
    installed outside it;
  - `--repos DIR` takes sources from local clones before any URL;
  - `lock --url` adds a component; a short revision resolves only in a
    local clone.
- **Version:** 0.2.0. The `tool tam-bundle` line is part of the bundle
  hash, so bundles sealed by 0.2.0 differ from 0.1.0's.

## Verification

- `make test` passes: the `tam-file-select` tests,
  `tests/tam-bundle/run.sh`, and the new `tests/tam-bundle/build.sh`, all
  against the sanitizer test build.
- Both bundle test scripts passed on tmpfs, Btrfs and ext4.
- `tests/tam-bundle/build.sh` uses three small local Git repositories and
  covers:
  - `lock`: starting a file, resolving a branch in a local clone, keeping
    comments, no-op and dry run, and its refusals;
  - `build`: commits, tools, needs and steps recorded in `BUNDLE`;
    build-only files left out; `SOURCE_DATE_EPOCH`, `LC_ALL` and `TZ`;
    the source repository untouched; the mirror and `--repos`;
  - the cache: a second build entirely from the cache with the same hash,
    and an empty cache from local clones with the same hash;
  - a new commit of a build-only dependency rebuilds its dependant;
  - switch into the slot, run the built command from `<slot>/current`,
    switch and roll back;
  - refusals: files outside the prefix, two components installing one
    file, a failing make (with the end of its log shown), a setuid file,
    an `after-build` cycle, `PREFIX` in a recipe, `@lib@` without
    `after-build`, an unknown build kind, a mismatched component name, a
    recipe without a lock line and the reverse, a short or missing commit,
    a missing `--version`, a relative slot, and a busy build cache; none
    leaves a bundle behind.
- **Trial with real components:** `libtam` 3f06e4f (build-only) and
  `tamlinux-tools` b07df33 built into a three-entry bundle in about 1.5 s.
- **Found and fixed:** at first that trial, run again under a different
  `$XDG_CACHE_HOME`, gave a different hash. `libtam` compiles with `-g`,
  so its build directory, inside the cache, was in the debug information
  linked into both programs. With fixed build paths, the same old `libtam`
  commit gives the same hash under two caches; with `libtam` 0.5.1
  (`-ffile-prefix-map`) it does too, and no build path is left in the
  programs.
- `tests/tam-bundle/build.sh` now records the paths a build sees, so its
  two same-hash checks fail if a cache path leaks in, and checks that a
  build-only dependency is read-only to its dependants.
