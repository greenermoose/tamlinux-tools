# Bugs folder

This repository owns the defect records for its software. Use one file per
bug: `0001-short-description.md`, with numbers local to this repository.
Cite the full repository/path when linking from another project. Check the
index and Git history before allocating a number; never reuse one.

Reports begin in `Reported`. Confirm a reproducible defect before calling it
`Confirmed`; use `In progress`, `Fix available`, `Verified`, `Deferred` or
`Closed` as work proceeds. A fix in source is `Fix available`, not evidence
that an installed build works. Close with verification evidence, or state
why the report is duplicate/rejected. Reopen when the behavior recurs.
Use the existing R1–R4 severity scale in plain words: critical, high, medium,
low; severity describes impact rather than confidence or scheduling.

Copy [_template.md](_template.md). Record affected/fixed versions and exact
verification scope. Link the code fix, regression check and any host-specific
investigation. Keep credentials, private user data, session identifiers and
machine-local evidence out of public records. Describe a synthetic reproducer
instead. Host investigations own physical/runtime evidence; this tracker owns
the software defect and source fix. Do not duplicate a defect in two trackers:
link its owner, even when its effects span components.

## Index

No defects recorded here yet. New entries link a record with its short title;
read the record for status rather than maintain duplicate status columns.
