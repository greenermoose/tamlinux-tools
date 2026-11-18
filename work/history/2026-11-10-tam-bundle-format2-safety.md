# tam-bundle format 2 development and process-test containment

- **Date**: 2026-11-10
- **CLI Tool**: Codex CLI (`codex`) `0.167.0`
- **Model**: `gpt-6.1-sol`
- **Transcript**: Retained privately by the author.
- **Authorship**: AI-assisted implementation in two consecutive sessions.
  The first wrote the initial format-2 and transaction code. The second
  preserved that work, corrected process selection, isolated signal tests,
  added regressions and completed build-tree verification.

## Guiding prompts

Initial development:

> Develop tam-bundle 0.6 per [redacted: private plan]'s 'Starting tam-bundle 0.6' section.

Recovery:

> Your last session kill hyprland by mistake. See the note from claude. What do you propose to do next?

> Proceed

## Incident and correction

The earlier process test terminated the live graphical session. Its apply
command used a temporary installation root, but the scanner independently
selected every executable named `Hyprland`. The test's `--yes` then sent
SIGTERM to the actual compositor. Temporary file roots did not contain
process signalling, and running the test outside the coding sandbox exposed
the live session. The earlier assertion that temporary roots made the test
safe was incorrect.

Process selection now compares complete paths in the selected installation's
old version tree and manifest-listed placed files. Arguments retain their
NUL-separated boundaries, mapped files are parsed as paths, and unrelated
prefixes or suffixes cannot authorize a signal. Deleted open files remain
detectable. The selected root is canonicalized before comparisons.

The live-root graphical-session check is separate: a running Hyprland blocks
apply, including first installation and `--yes`. A discovery pass detects
that blocker before any signal is sent, leaving both the compositor and
other session programs alone. Temporary installations ignore the executable name.

The Python signal suite re-executes inside unprivileged user, mount and PID
namespaces with a freshly mounted `/proc`. It requires PID 1 and a matching
procfs view before any test setup. There is no environment flag to bypass
this check and no fallback to a host-process run if namespace setup fails.
The kernel prevents these tests from addressing processes outside the PID
namespace, independently of scanner correctness.

## Development work retained

The recovered 0.6.0 draft adds format-2 manifests and portable archives,
prepare/apply/cancel transactions, installed identities, retained placed-file
copies, required-step retry and durable-transition fault points. Format-1
reading and the existing recipe build interface remain covered. The manual
describes the added interface, and `make test` includes the new suite.

This is build-tree development. It does not install or promote this draft,
change assembly pins, complete the broader bundle migration, or establish
real-machine acceptance of transaction recovery. The remaining implementation
format review and migration steps stay separate from this safety correction.

## Verification

- `make test` passed on 2026-11-10. Both the format-2 executable and busy-check
  fixture used AddressSanitizer and UndefinedBehaviorSanitizer.
- All 14 format-2 tests passed, including the unrelated `Hyprland` helper,
  path-prefix/suffix lookalikes, a placed executable, interpreted programs,
  working directories, open descriptors and mappings with spaced paths and
  deleted entries, and the live-root graphical blocker, including a
  compositor in the command's ancestor chain.
- Interruption coverage passed at 38 prepare and 31 apply durable
  transitions, plus 12 randomized SIGKILL trials.
- An independent test kept an outside sentinel alive while proving that
  the namespace could neither see its `/proc` entry nor obtain its pidfd.
  A direct host-context call to the namespace guard was refused.
- The existing format-1 suite, build/lock/cache tests, file-selector tests,
  manual formatting/help coverage, and polkit XML checks passed. The
  format-1 suite also recovered after its 30 randomized kill trials.
- The first full run found missing manual descriptions for the draft's new
  commands/options; those descriptions were added before the passing run.
- Final review strengthened the graphical blocker to prevent stopping any
  client of a live session. All 14 affected format-2 tests passed again
  after that refinement.
- The installed tool and product files were not changed by this work.
