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
| **`tam-file-select`** | Fast native file chooser delegating to the desktop XDG Portal | `libtam`, `libdbus-1` |

## Building & Installation

`tamlinux-tools` links against an **installed** `libtam` under the same
`PREFIX` (`/usr/local` by default):

```bash
# Build all tools
make

# Run the test suite
make test

# Install to system (defaults to /usr/local/bin)
sudo make install PREFIX=/usr/local
```

For a development build for your user, install `libtam` under the same
prefix first; `make uninstall` with the same `PREFIX` removes it again:
```bash
make install PREFIX=$HOME/.local/dev
make uninstall PREFIX=$HOME/.local/dev
```

To use a custom `libtam` installation path:
```bash
make LIBTAM_PREFIX=/path/to/libtam
```

## License

GNU General Public License v3.0 (GPLv3). See [LICENSE](LICENSE) for details.
