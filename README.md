# tamlinux-tools

Native Tamlinux command-line utilities that consume
[`libtam`](https://github.com/greenermoose/libtam).

## Tools

| Command | Description | Dependencies |
| :--- | :--- | :--- |
| **`tam-file-select`** | File chooser via the desktop XDG Portal | Static `libtam-portal` / `libtam`; shared `libdbus-1` at runtime |

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
