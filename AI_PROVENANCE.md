# AI Collaboration & Provenance

Fred directs this project. AI-assisted work records the tool and verified
version, model, guiding prompts, authorship boundaries and verification.
Session transcripts are retained privately by the author; public records carry
no transcript identifiers or store paths.

AI-assisted commits carry `Co-authored-by`, `AI-Tool` and `AI-Model` trailers.
Human-only commits retain their original authorship.

## How to read this record

This repository follows the Tamlinux
[AI provenance standard](https://github.com/greenermoose/tamlinux/blob/main/docs/ai-provenance-standard.md).
Each session record in [`docs/ai/`](docs/ai/) gives the date, tool version,
model, Fred's guiding prompts verbatim, the commits, and the decisions.

| Date | Work | Tool and model | Evidence |
| --- | --- | --- | --- |
| 2026-11-10 | Finish 0.6 prepared renames, recovery refusal and concrete format review | Codex 0.167.0; `gpt-6.1-sol` | [Session record](docs/ai/2026-11-10-tam-bundle-06-completion.md) |
| 2026-11-10 | Recovered format-2 draft; correct process selection and isolate signal tests after a compositor termination | Codex 0.167.0; `gpt-6.1-sol` | [Session record](docs/ai/2026-11-10-tam-bundle-format2-safety.md) |
| 2026-11-09 | `tam-bundle` 0.5.0–0.5.2: recipe targets, pinned files and native bring-up | Codex 0.167.0; `gpt-6.1-sol` | [Session record](docs/ai/2026-11-09-tam-bundle-recipes.md) |
| 2026-10-09 | Public repository publication and README `libtam` link fix | opencode 1.18.35; `big-pickle` | [Session record](docs/ai/2026-10-09-publish-repository.md) |
| 2026-10-19 | Find `libtam` with pkg-config; clear error when missing | Claude Code 2.1.304; `claude-opus-5-5` | [Session record](docs/ai/2026-10-19-libtam-pkg-config.md) |
| 2026-10-22 | Link `libtam` and its new portal library, `libtam-portal`, statically | Claude Code 2.1.307; `claude-opus-5-5` | [Session record](docs/ai/2026-10-22-libtam-portal-static.md) |
| 2026-11-04 | Clarify repository purpose in README (no ROADMAP) | Cursor 3.27.22; `composer` | [Session record](docs/ai/2026-11-04-clarify-readme-purpose.md) |
| 2026-11-09 | Field survey for `tam-bundle`; add `UPSTREAM.md` and `upstream/` | Claude Code 2.1.321; `claude-opus-5-5` | [Session record](docs/ai/2026-11-09-tam-bundle-field-survey.md) |
| 2026-11-09 | Design for `tam-bundle` | Claude Code 2.1.321; `claude-opus-5-5` | [Session record](docs/ai/2026-11-09-tam-bundle-design.md) |
| 2026-11-10 | Revised `tam-bundle` design: standard placement, prepare and apply, settings capture | Claude Code 2.1.322; `claude-opus-5-5` | [Session record](docs/ai/2026-11-10-tam-bundle-redesign.md) |
| 2026-11-09 | `tam-bundle` 0.1.0: formats, switch, rollback, recovery and their tests | Claude Code 2.1.321; `claude-opus-5-5` | [Session record](docs/ai/2026-11-09-tam-bundle-core.md) |
| 2026-11-09 | `tam-bundle` 0.2.0: `build` and `lock` | Claude Code 2.1.321; `claude-opus-5-5` | [Session record](docs/ai/2026-11-09-tam-bundle-build-lock.md) |
| 2026-11-09 | `tam-bundle` 0.3.0 and 0.3.1: privilege through `pkexec` | Claude Code 2.1.321; `claude-opus-5-5` | [Session record](docs/ai/2026-11-09-tam-bundle-pkexec.md) |
| 2026-11-09 | `tam-bundle` 0.4.0: the manual index, built into the bundle | Claude Code 2.1.321; `claude-opus-5-5` | [Session record](docs/ai/2026-11-09-tam-bundle-man-index.md) |
| 2026-11-09 | `tam-bundle`: the manual page | Claude Code 2.1.321; `claude-opus-5-5` | [Session record](docs/ai/2026-11-09-tam-bundle-man-page.md) |
| 2026-11-09 | `tam-bundle` 0.4.1: every changing command recovers first | Claude Code 2.1.321; `claude-opus-5-5` | [Session record](docs/ai/2026-11-09-tam-bundle-recover-first.md) |

The initial commit `18ef5de` predates this record. It was authored in an
Antigravity session and carries that session's `AI-Tool` and `AI-Model`
trailers in the commit message itself; no separate session record was
published with it.
