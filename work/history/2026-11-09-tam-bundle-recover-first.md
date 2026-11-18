# `tam-bundle` 0.4.1: every changing command recovers first

- **Date:** 2026-11-09 (UTC 2026-11-10)
- **Stage:** Develop: a correctness fix and the matching design and manual
  text.
- **CLI Tool:** Claude Code 2.1.321
- **Model:** Claude Opus 5.5 (`claude-opus-5-5`)
- **Transcript:** Retained privately by the author.
- **Commits:** the commit that adds this record.
- **Authorship:**
  - Fred asked whether the design or the code should change, asked for
    research and a recommendation, accepted it, and asked for the changes.
  - Claude reproduced the fault, researched, wrote the fix, the tests, the
    design and manual text, and this record.
- **Checked against:** `src/tam-bundle/main.c`, `switch.c`, the design,
  the SQLite locking and atomic-commit documents and the Linux ext4
  documentation, and runs of the test build with fault points.

## Guiding prompts

> Ponder this question: shoudl the design be corrected or the code changed? Do some research. Then give me your recommendation backed up with evidence from credible authorities.

> I accept your recommendation. Make those changes.

## What was wrong

The design said every command runs the recovery check first; only `switch`
and `back` did. With the test build stopped right after the exchange of a
`back` to an unprotected kept tree, the tree that had been live sat in
`kept/` under the target's name. `prune --yes` then removed it, logging the
target's name; `recover` recorded the removed tree as previous, and `back`
failed. Separately, a dry-run `switch` or `back` finished an interrupted
switch for real, although a dry run promises to change nothing.

## What changed

- **`tb_recover_first`** in `switch.c`: under the lock, every changing
  command (`switch`, `back`, `keep`, `unkeep`, `prune`) finishes or undoes
  an interrupted switch first. A dry run only says what recovery would do
  and exits 4.
- **`check`, `where` and `after`** read only the live tree, which the
  exchange keeps whole, so they still answer, with a warning naming the
  interrupted switch. `status` already exited 4.
- **Design and manual:** the rule is stated per command, with the reason
  and the journaling precedent (SQLite rolls back a hot journal before
  reading; ext4 replays its journal at mount).
- **Version:** 0.4.1.

## Verification

- New tests in `tests/tam-bundle/run.sh`: prune after an interrupted back
  keeps the tree that was live and `back` returns to it; dry-run `prune`,
  `switch`, `back` and `keep` exit 4 and leave the intent in place; `check`,
  `where` and `after` answer and warn; `keep` and `unkeep` recover first.
- The same tests run against a test build of the previous commit fail at
  each of these points.
- `make test` passes; `tests/tam-bundle/run.sh` passes on tmpfs, Btrfs and
  ext4.
