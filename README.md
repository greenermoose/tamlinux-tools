# tamlinux-tools

`tamlinux-tools` is the official repository for native Tamlinux command-line utilities and system helpers.

## Overview & Architecture

Tamlinux tools are designed to be fast, minimal, and transparent:
- Built in clean C against the [`libtam`](https://github.com/greenermoose/libtam) foundation library.
- Sub-megabyte resident memory (RSS) and instant 0–2 ms cold start.
- Native compilation against both `glibc` and `musl` on Void Linux, Arch, and antiX.
- Zero unnecessary runtime bloat or heavy GUI toolkits.

## Tools in this Suite

| Command | Description | Dependencies |
| :--- | :--- | :--- |
| **`tam-file-select`** | Fast native file chooser delegating to the desktop XDG Portal | `libtam-portal` and `libtam` (static), `libdbus-1` |

## Building & Installation

`tamlinux-tools` links statically against an **installed** `libtam` 0.4.0 or
later. `tam-file-select` uses its portal library, found with
`pkg-config libtam-portal`, which adds `libtam` and D-Bus. Install `libtam`
first; if its prefix is outside
pkg-config's default path, add it, for example
`export PKG_CONFIG_PATH="$HOME/.local/dev/lib/pkgconfig"`. If pkg-config cannot
find it, the build stops and says what to do.

```bash
# Build all tools
make

# Run the test suite
make test

# Install to system (defaults to /usr/local/bin)
sudo make install PREFIX=/usr/local
```

For a development build for your user; `make uninstall` with the same
`PREFIX` removes it again:
```bash
make install PREFIX=$HOME/.local/dev
make uninstall PREFIX=$HOME/.local/dev
```

To name the `libtam` install prefix instead of asking pkg-config:
```bash
make LIBTAM_PREFIX=/path/to/libtam
```

## License

GNU General Public License v3.0 (GPLv3). See [LICENSE](LICENSE) for details.
