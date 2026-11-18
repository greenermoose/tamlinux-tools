# Preserve completed transactions when preparation is refused

- **Tool:** Codex CLI 0.169.0
- **Model:** gpt-6.1-sol
- **Transcript:** Retained privately by the author.
- **Prompt (excerpt):** “Trial artifacts are under /tmp, so verify they still exist and match their recorded hashes before use.”

An authorized workstation ownership proof exposed that a refused prepare
renamed the completed transaction to `last-transaction` before checking the
incoming product's runtime requirements and placed-file ownership. The live
product and settings remained intact, but the refusal changed retained journal
state and could replace the older completed history entry.

Preparation now archives the completed record only after those checks pass,
immediately before arming the new transaction. Both completed records remain
byte-identical after refusals for an unowned path, native ownership, a missing
runtime dependency and an unexpected full bundle hash. A valid retry succeeds.
The generic failure line no longer formats stale errno as “Success”. The
independent bootstrap tool version becomes 0.13.1; bundle and record formats
are unchanged.

The new regression first reproduces the missing completed record on the old
engine. All 13 runtime/ownership tests pass with the correction, inside private
PID/proc namespaces. The full native-tools `make test` suite also passes,
including transaction faults, settings, builds and runtime contracts. Machine
installation of the corrected bootstrap and the repeated ownership proof remain
separately approved operations.
