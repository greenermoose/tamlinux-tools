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

Assemblies install `tam-file-select` on `PATH`. Product flows that call it
include share (`tam-menu-share`) and theme branding image pick.

## Usage: `tam-file-select`

```text
tam-file-select [--title <title>] [--multiple] [--directory] [--extensions "<ext ext...>"]
```

Prints selected path(s), one per line. Exit `0` on pick, `1` if nothing was
picked, `2` if the chooser failed to open.

## Building and installation

Links statically against an **installed** `libtam` 0.4.0 or later.
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
