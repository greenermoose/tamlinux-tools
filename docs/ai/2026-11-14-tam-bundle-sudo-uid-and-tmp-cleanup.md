# Accept `SUDO_UID` during `tam-bundle` prepare and clean `/tmp` archive temp dirs from any CWD

Fixes two `tam-bundle` issues encountered during kernel-console testing:

- **`prepare_uid()` (`src/tam-bundle/transaction.c`):** Falls back to `SUDO_UID`
  when `getuid() == 0` and `PKEXEC_UID` is unset, validating numeric UID ranges
  and reporting invalid values in either variable. This matches
  `desktop_uid()` in `after.c` so `sudo tam-bundle install ...` records the
  invoking desktop user's UID (`uid 1000`) rather than `0` cleanly for
  unprivileged `apply-settings` and `session-ready`.
- **`tb_remove_path()` (`src/tam-bundle/tb.h`, `src/tam-bundle/slot.c`):**
  Adds an explicit root/CWD-safe tree removal helper and uses it in `main.c`
  (`verify` and `needs` on `.tbundle` archives), `transaction.c` (`prepare` on
  `.tbundle` archives), and `settings.c` (`export` and `import` temp dirs).
  Previously those callers passed `tb_remove_at(AT_FDCWD, "/", temp + 1)`, which
  opened `<cwd>/tmp` instead of `/tmp` whenever `cwd != /` and leaked
  temporary unpacked directories under `/tmp`.
- **Regression tests (`tests/tam-bundle/format2.py`):** Updates
  `test_privileged_prepare_records_the_invoking_user` to cover `SUDO_UID`
  recording, invalid `SUDO_UID` rejection, and `PKEXEC_UID` precedence, and adds
  `test_archive_temp_cleanup_when_cwd_is_not_root` verifying `verify`, `needs`,
  `install`, and failing `verify`/`install` remove temporary directories under
  `/tmp` when `cwd != /`.

Verification: Full `make test` passed (14 journal tests, 34 format-2 tests, 32
settings tests, 12 runtime tests, core/build/fault/namespace checks, manual
checks, and polkit XML validation) inside temporary directories and private PID
namespaces.

- **Date:** 2026-11-14
- **CLI Tool:** Antigravity CLI (`agy`) `1.5.9`
- **Transcript:** Retained privately by the author.
- **Authorship:** AI-assisted implementation, documentation and tests.

## Guiding prompts

> I'm running in a kernel console virtual terminal. pkexec doesn't work there. That's why I have to do sudo. Should I start a graphical desktop? Or can you revise so that I can do everything from a kernel console.
>
> That needs to be put in context. AI agents that are asking for escalated privileges when I'm using them on the graphical desktop need to use pkexec with absolute paths. But when I'm running commands in a kernel console, I use sudo.
