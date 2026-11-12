# Dormant slot migration and exact product detachment

The uninstalled 0.12.0 tool verifies and archives a dormant format-1 slot,
retains exact rollback copies during detachment for Nix return, and repairs
settings preparation retries. The [design checkpoint](../plans/tam-bundle.md#012-migration-source-checkpoint)
records the concrete transaction extension and remaining migration gates.

- **Date**: 2026-11-11
- **CLI Tool**: Codex CLI (`codex`) `0.167.1`
- **Model**: `gpt-6.1-sol`
- **Transcript**: Retained privately by the author.
- **Authorship**: AI-assisted implementation, verification and documentation.

## Guiding prompt (excerpts)

> Continue Plan 53 toward making tam-bundle the Tamlinux delivery mechanism on [redacted: workstation].

> Next, finish Home Manager link conversion, dormant-slot handling and tam-deploy bundle preparation, activation and return to the exact saved Nix generation.

> Nothing was deployed. Preserve Nix/Home Manager delivery and rollback and the installed tam-bundle 0.5.2 pin. Preserve mandatory PID namespaces/private /proc in tests, with no host-process fallback.

## Implemented

- `migrate-dormant` verifies the full format-1 identity and payload, refuses
  busy or edited trees and pending legacy work, holds the legacy lock, and
  retains the old current/kept trees under `legacy-v1`. An intent record
  makes archival retries repeatable. No old identity becomes format 2.
- `detach` binds the exact format-2 product hash. Its existing durable system
  transaction targets `-` while files and the current link leave. Full
  rollback-copy checks, native ownership and live-file comparisons precede
  removal. Incomplete removal blocks startup and retries forward; retained
  versions and archives stay available.
- Status exposes the prepared target's full hash. User preparation retains an
  already matching in-flight plan and capture, and can repair a missing plan
  after the system has applied. Other users and unrelated transactions refuse.

## Verification

The final full `make test` passed on 2026-11-12 with an exclusive checkout claim. ASan/UBSan
reported no finding. All process tests retained mandatory private PID and
private-proc containment, with no host-process fallback.

- 33 product cases, including 11 enumerated dormant archival boundaries and
  27 detach boundaries; all were retried and followed by a checked install.
- 28 settings cases, including a preserved in-flight CHANGES/capture record
  and first-install recovery after system apply without an earlier user plan.
- 12 runtime cases, including the fully static production tool.
- Existing product/settings fault enumeration and randomized kills, format-1
  compatibility, build/lock/cache and publication proofs, file selector,
  manual coverage/formatting and polkit XML checks passed.

The first full run reached the manual coverage check and found the two new
commands undocumented; their descriptions were added before the final passing
run. Temporary-root tests and the delivery coordinator's fixture results do
not establish native package replacement, physical console authorization,
login, power-loss safety, second-workstation acceptance or actual Nix return.

The legacy recipe pin remains 0.5.2; this source work does not establish an
installed executable identity. Reconcile that identity before a live migration.
Nix/Home Manager remains the delivery mechanism. No privileged installation, Test, Run or Release occurred.
The bootstrap package must be rebuilt from the exact reviewed tool before any
real candidate can use these interfaces.
