# Find libtam with pkg-config

- **Date:** 2026-10-19
- **Time:** 21:33 to 21:50 -04:00
- **Stage:** Develop: the Makefile finds `libtam` with pkg-config and stops with instructions when it is missing
- **CLI Tool:** Claude Code 2.1.304
- **Model:** Claude Opus 5.5 (`claude-opus-5-5`)
- **Transcript:** Retained privately by the author.
- **Commits:** `e864442` and the commit that adds this record; with `libtam` `aada139` (installs `libtam.pc`, version 0.3.0)
- **Authorship:** Fred chose pkg-config discovery with a clear error while working on `tam`; Claude Code applied the same change here.
- **Checked against:** `Makefile`, the `libtam` Makefile, the `tam` Makefile

## Guiding prompts

> Yes, go ahead with pkg-config and the clear error

The full discussion is quoted in the `tam` repository's record of the same
session, `docs/ai/2026-10-19-tam-libtam-pkg-config.md`.

## Key decisions

`LIBTAM_PREFIX` no longer defaults to `PREFIX`; it remains a direct override
that skips pkg-config.

## Verification

With no `libtam` visible, `make` stops with the instructions. With
`PKG_CONFIG_PATH` naming a `libtam` 0.3.0 install, and separately with
`LIBTAM_PREFIX`, `make test` passes and `tam-file-select` carries the matching
RUNPATH.
