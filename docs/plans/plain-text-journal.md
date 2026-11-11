# Plain text journal

Status: Implement the approved daily-file and Micro interface; source checks
are separate from packaged/physical acceptance.

`tam-journal [add] [--date YYYY-MM-DD]` is a standalone C11/libtam tool.
With no subcommand it edits today's journal; `add` starts a blank note.
`--date` selects the destination day. Resolve the default day and timestamp
once at capture start. Use `TAM_JOURNAL_DIR`, default `$HOME/Journal`, and
ordinary UTF-8 `YYYY-MM-DD.txt` files. Capture timestamps include actual date,
time and numeric timezone even when the selected journal day is different.

Micro edits durable private drafts, not the only copy of existing text.
After successful editor exit, an unchanged edit or blank note cancels.
A per-day advisory lock serializes tool-managed edits/commits; notes can be
composed concurrently. A busy day refuses the commit and retains the draft.
Recheck externally changed text before replacing a full-day edit. Save by
same-directory atomic replacement, sync data and directory, and retain the
draft on any error. A crash can leave a draft; it must not truncate the
existing day. Symlink/nonregular targets and invalid UTF-8 are refused.
Limit one day/draft to 16 MiB to bound memory use; no background processes.

Directories/files are created with 0700/0600. An existing root must be owned
by the current user and private; never silently change a shared folder's
permissions. Interactive Micro has no wall-clock deadline. Missing editor,
cancel, error and success have distinct outcomes. Exit codes: 0 success or
cancel, 1 data/editor failure, 2 invalid usage, 3 conflicting/unavailable lock.

Desktop entry points belong to the product: calendar selected-date actions,
Quick Launch and a shortcut through a small floating terminal. There is no
new bar widget. Journal files are user data and stay outside package rollback.

Acceptance: synthetic editors exercise dates/leap years, UTF-8, cancellation,
failed editors, symlinks, concurrent edits/notes, outside modifications,
permissions and failed saves with draft retention. The developer separately
checks real Micro on a text console and desktop through a packaged candidate.
