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
| 2026-10-09 | Public repository publication and README `libtam` link fix | opencode 1.18.35; `big-pickle` | [Session record](docs/ai/2026-10-09-publish-repository.md) |

The initial commit `18ef5de` predates this record. It was authored in an
Antigravity session and carries that session's `AI-Tool` and `AI-Model`
trailers in the commit message itself; no separate session record was
published with it.
