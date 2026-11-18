# Clarify repository purpose in README

- **Date:** 2026-11-04
- **Time:** review and commit morning of 2026-11-04 -05:00
- **Stage:** Develop: rewrite `README.md` so readers can tell what this
  repository is for, what belongs elsewhere, and what ships today
- **CLI Tool:** Cursor (`cursor`) `3.27.22`
- **Model:** Composer (`composer`)
- **Transcript:** Retained privately by the author.
- **Commits:** the commit that adds this record
- **Authorship:** Fred asked to improve the README or add a ROADMAP because
  the purpose was unclear; accepted a README rewrite (no ROADMAP) after Micro
  review and asked to commit. Cursor wrote the draft.
- **Checked against:** existing `README.md`, sibling `libtam` and `tam`
  READMEs, product callers of `tam-file-select`

## Guiding prompts

> Either improve the README.md in tamlinux-tools or create a ROADMAP.md file
> in that repo. It is not clear what it is for as it is now.

> Clarify tamlinux-tools purpose in README
>
> Implement the plan as specified, it is attached for your reference. Do NOT
> edit the plan file itself.

> accept. commit.

## Key decisions

- Improve `README.md`; do not add `ROADMAP.md` (no scheduled tools milestones).
- State purpose, admission rules, and the boundary with `tamlinux` product
  commands, `tam`, `libtam`, and `tamlinux-packages` in plain public words.
- Keep the tools table, `tam-file-select` usage, and build instructions; note
  that assemblies install the tool and product share/theme flows call it.

## Verification

Fred reviewed `README.md` in Micro and accepted it. Docs-only change; no
build.
