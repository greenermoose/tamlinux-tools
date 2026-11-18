# Link libtam and libtam-portal statically

- **Date:** 2026-10-22
- **Time:** 06:34 to 06:43 -04:00
- **Stage:** Develop: `tam-file-select` builds against `libtam` 0.4.0's
  separate portal library, `libtam-portal`, and links both statically
- **CLI Tool:** Claude Code 2.1.307
- **Model:** Claude Opus 5.5 (`claude-opus-5-5`)
- **Transcript:** Retained privately by the author.
- **Commits:** the commit that adds this record; with `libtam` `a051ebf`
  (0.4.0)
- **Authorship:** Fred approved a reviewed refactoring sequence for `tam` and
  `libtam`, accepted Claude's recommendation to link `libtam` statically, and
  asked why the portal is a separate library. Claude wrote the changes.
- **Checked against:** `Makefile`, `tests/run_tests.sh`,
  `src/tam-file-select.c`, the `libtam` Makefile and headers, `ldd` and `nm`
  of the built binary

## Guiding prompts

> I approve the sequence. What do you recommend regarding static linking?
> Add a man-page cache. In general, we want to use disk space to free up CPU
> cycles and RAM.

> I accept your recommendation. Start step 1 on that basis.

> Explain the portal stuff. Why that separate library?

## Key decisions

- `libtam` 0.4.0 moves the D-Bus file-chooser portal into its own static
  library, so programs that do not use it never link or load D-Bus.
  `tam-file-select` is the only user; it includes `<tam_portal.h>` and links
  with `pkg-config libtam-portal`, or `-ltam-portal -ltam` and D-Bus under
  `LIBTAM_PREFIX`.
- Static linking removes the RUNPATH; the configuration stamp still forces a
  rebuild when the `libtam` in use changes.
- The link test now requires that no shared `libtam` is loaded and that the
  portal code is in the binary.

## Verification

`make test` passes when `libtam` 0.4.0 is found through pkg-config, and again
with `LIBTAM_PREFIX`. `ldd bin/tam-file-select` lists only D-Bus, libsystemd,
libgcc_s and libc; the binary is 27,048 bytes.
