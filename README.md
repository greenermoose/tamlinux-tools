# tamlinux-tools

Standalone Tamlinux command-line utilities written in pure C against
[`libtam`](https://github.com/greenermoose/libtam). Each tool is a small
program with its own component version, delivered through
[`tamlinux-packages`](https://github.com/greenermoose/tamlinux-packages)
assemblies, and usable from scripts without the Tamlinux desktop shell.

## What belongs here

A tool belongs in this repository when it is:

- Implemented in pure C and linked against `libtam` (and `libtam-portal` only
  when the tool needs the desktop portal)
- Small and reusable from other programs or shell scripts
- Independent of the Tamlinux Quickshell shell, compositor adapters, and
  product menus
- Versioned with this component, not with the Tamlinux product number

Bash or Python scripts are not added here. Rewrite them in C with `libtam`
first if they should become tools in this suite.

## What belongs elsewhere

| Need | Repository |
| :--- | :--- |
| Workstation / product `tam-*` commands (menus, theme, deploy, compositor helpers, lifecycle tools) | [`tamlinux`](https://github.com/greenermoose/tamlinux) under `commands/` |
| Offline knowledge browser and guide pager | [`tam`](https://github.com/greenermoose/tam) |
| Shared C foundation library | [`libtam`](https://github.com/greenermoose/libtam) |
| Package recipes and assembly pins | [`tamlinux-packages`](https://github.com/greenermoose/tamlinux-packages) |

## Tools

| Command | Description | Dependencies |
| :--- | :--- | :--- |
| **`tam-file-select`** | File chooser via the desktop XDG Portal | Static `libtam-portal` / `libtam`; shared `libdbus-1` at runtime |
| **`tam-bundle`** | Installs a named, versioned set of real files into a slot, records it, and switches between versions with rollback ([design](docs/plans/tam-bundle.md)). In development; not yet delivered by any assembly. | Static `libtam` 0.5.0 or later; libc only. `build` and `lock` run `git`, and `build` runs `tar` and `make`; `pkexec` (polkit) for a root-owned slot |

Assemblies install `tam-file-select` on `PATH`. Product flows that call it
include share (`tam-menu-share`) and theme branding image pick.

## Usage: `tam-file-select`

```text
tam-file-select [--title <title>] [--multiple] [--directory] [--extensions "<ext ext...>"]
```

Prints selected path(s), one per line. Exit `0` on pick, `1` if nothing was
picked, `2` if the chooser failed to open.

## Usage: `tam-bundle`

```text
tam-bundle seal <dir> --name N --version V [--source "COMPONENT COMMIT"]... [--after STEP]...
tam-bundle verify <dir>
tam-bundle [--slot DIR] switch <bundle dir>
tam-bundle [--slot DIR] back [<bundle id>]
tam-bundle [--slot DIR] status | check | where <path> | recover | prune [--yes]
tam-bundle lock <component> <commit> [--lock FILE] [--url URL] [--repos DIR]
tam-bundle [--slot DIR] build <recipes dir> <lock file> -o <dir> --version V [--repos DIR]
```

`lock` sets one component's exact commit in a lock file (default
`tamlinux.lock`); a short revision is resolved only in a local clone named
with `--repos`. `build` exports each locked commit with `git archive`, runs
`make` and `make install` for it, caches finished components under
`$XDG_CACHE_HOME/tamlinux/tam-bundle/`, and seals the merged result for the
slot's live tree. Each `make` runs in its own user and mount namespace at
fixed paths under `/tmp/tam-bundle`, so the same lock gives the same bundle
whatever the cache's path; this needs unprivileged user namespaces.
`--repos DIR` takes sources from local clones in `DIR` before fetching from
a lock file's URL.

On a slot that belongs to root (the default), `switch`, `back`, `recover`,
`keep`, `unkeep` and `prune` run again as root through `pkexec`, then run
the session steps (such as restarting the desktop shell) as you. Where no
polkit agent runs, as on a text console, run the command with `sudo` and
then `tam-bundle after` as yourself. `make install` also installs the
polkit action, `share/polkit-1/actions/org.tamlinux.bundle.policy`.

`tam-bundle --help` lists every command and option. The default slot is
`/usr/lib/tamlinux`; any directory works as a slot, so tests and private
tools use slots of their own. Exit `0` on success, `1` when refused or a
check finds problems, `2` on usage errors, `3` when a switch completed but a
post-install step failed, and `4` when an interrupted switch needs
`tam-bundle recover`.

`make test` builds `bin/tam-bundle-test`, which honours test fault points,
and runs `tests/tam-bundle/run.sh` and `tests/tam-bundle/build.sh` (which
needs `git` and `make`) in `$TAM_TEST_DIR` (default `$TMPDIR`).
Run it on Btrfs and on ext4 directories to cover both file systems.

## Building and installation

Links statically against an **installed** `libtam`: 0.4.0 or later for
`tam-file-select`, 0.5.0 or later for `tam-bundle`.
`tam-file-select` uses the portal library via `pkg-config libtam-portal`
(adds `libtam` and D-Bus). Install `libtam` first; if its prefix is outside
pkg-config's default path:

```bash
export PKG_CONFIG_PATH="$HOME/.local/dev/lib/pkgconfig"
```

```bash
make
make test
sudo make install PREFIX=/usr/local
```

Development install for your user (`make uninstall` with the same `PREFIX`):

```bash
make install PREFIX=$HOME/.local/dev
```

Or name the `libtam` prefix without pkg-config:

```bash
make LIBTAM_PREFIX=/path/to/libtam
```

Portability notes for musl or other distributions belong with `libtam`; this
repo does not claim measured cold-start times or verified musl builds.

## License

GNU General Public License v3.0 (GPLv3). See [LICENSE](LICENSE).
