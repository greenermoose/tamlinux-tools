# Plain text README conversion

- **Date:** 2026-11-13
- **Time:** 20:15 to 20:38 -0500 (session window).
- **Stage:** Develop; documentation and its filename consumers.
- **CLI Tool:** Codex CLI 0.169.0, checked from the running binary.
- **Model:** GPT-6.1 Sol (`gpt-6.1-sol`), verified from the session runtime.
- **Transcript:** Retained privately by the author.
- **Commits:** The commit adding this record.
- **Authorship:** Fred approved the conversion plan; Codex converted 2
  tracked README files and updated affected consumers under his direction.
- **Checked against:** The tracked originals at `3096a17a3775`,
  the converted README files, active references and affected build/test files.

## Guiding prompt

> Read the approved plan at [redacted: local path] and implement it.

## Decisions

Use relative repository links and readable section names. Preserve original
commands, factual claims, attribution, historical filename records and upstream
filenames. This conversion does not change component versions or source pins.

## Verification

Both README conversions compared against their originals; code blocks and table cells checked; local links audited; diff reviewed.

Original prose, table cells, URLs and fenced examples were compared with the
replacements; filename edits inside examples were limited to converted targets.
The link audit found no newly missing targets. Pre-existing missing targets
remain outside this conversion. Historical Markdown filenames remain in
immutable-source compatibility code and tests where applicable.

`git diff --check` passes.
