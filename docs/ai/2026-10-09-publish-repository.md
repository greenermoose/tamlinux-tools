# Publish tamlinux-tools and fix the libtam README link

- **Date:** 2026-10-09
- **Stage:** Publication (first public push) plus a documentation fix.
- **CLI Tool:** opencode 1.18.35
- **Model:** `big-pickle`
- **Transcript:** Retained privately by the author.
- **Commit:** `ef58902` ("Link libtam across repositories in README").
- **Authorship:** Fred asked for publication and the link fix; opencode created
  the GitHub repository, pushed the existing history, rewrote the link and
  wrote the provenance records. No delegation.

## Guiding prompts

> Create a public repo on GitHub for tamlinux-tools and push the local repo.

> Rewrite the README link for libtam to link across repos on GitHub. There is
> a public libtam repo.

## Decisions and scope

- Published as `greenermoose/tamlinux-tools`, public, default branch `main`,
  description "Native Tamlinux command-line utilities and system helpers".
- The pre-existing initial commit `18ef5de` was pushed unchanged. It was
  authored in an earlier Antigravity session and carries its own `AI-Tool`
  and `AI-Model` trailers; this session did not amend it and published no
  session record for it.
- The README overview bullet used the relative path `../libtam`, which leaves
  the repository when rendered on GitHub and therefore resolves to nothing.
  It now links to the public `libtam` repository by its full URL, so the
  cross-repository reference works on GitHub and in any other renderer.
- The repository had no AI provenance files, so this session added
  `AI_PROVENANCE.md`, this record, and the `docs/ai/` index. The session ID
  for this record lives only in the private ledger.

## Verification

- `gh repo view greenermoose/tamlinux-tools` reports `PUBLIC` with default
  branch `main`.
- `git ls-remote origin` reports `main` at `18ef5de`, and `git status -sb`
  reports `main...origin/main` with no divergence before this push.
- `gh repo view greenermoose/libtam` resolves, so the rewritten link target
  exists and is public.
