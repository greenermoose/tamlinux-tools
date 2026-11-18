# Toggles overlap regression coverage and full Step 1 verification

Adds a `tam-bundle` settings regression test confirming that when
`SETTINGS` lists `common .local/state/tamlinux/toggles/` before
`host .local/state/tamlinux/toggles/hypr/`, cross-machine export/import
preserves sibling `common` toggle files from the source capture while retaining
the target machine's `toggles/hypr/` overrides.

Verification: full `make test` with the rebuilt `libtam` 0.7.0 prefix (14
journal tests, `tamlinux-tools` CLI tests, `tam-bundle` core/build/fault/format2/
namespace/runtime tests, 32 settings tests, manual checks, and polkit XML
well-formedness check). All tests run only inside temporary directories and
mandatory private PID namespaces; no installed system files were modified.

- **Date:** 2026-11-13
- **CLI Tool:** Antigravity CLI (`agy`) `1.5.8`
- **Transcript:** Retained privately by the author.
- **Authorship:** AI-assisted test addition and suite verification.

## Guiding prompts

> Plan the next milestone for [redacted: private backlog reference].
>
> yes, tam-deploy should check the hosts first and fall back
>
> Approve. proceed
