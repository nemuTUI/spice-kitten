# spice-kitten

SPICE client that renders remote VM output using the [Kitty Graphics Protocol](https://sw.kovidgoyal.net/kitty/graphics-protocol/).

Instead of opening a conventional graphical window, spice-kitten converts SPICE display bitmaps into PNG images and displays them inline in your terminal — no GTK, SDL, or VTE required.

<img width="371" height="557" alt="image" src="https://github.com/user-attachments/assets/bf1a5534-1cbe-4316-909a-b350291c179f" />

## Features

- **In-terminal display** — renders VM output via Kitty graphics escape sequences
- **X11 input backend** — grab keyboard/pointer and forward events to the SPICE server
- **Threaded SPICE channels** — separate threads for Main, Display, and Screen rendering
- **Mouse coordinate scaling** — maps terminal dimensions to VM display automatically
- **RSA-OAEP ticket encryption** — secure SPICE authentication
- **Shared-memory rendering** — PNG data written to POSIX shm for zero-copy terminal display

## Requirements

| Dependency | Purpose |
|---|---|
| `spice-protocol` | SPICE protocol definitions |
| `libX11` | X11 input backend |
| `openssl` / `libcrypto` | RSA password encryption |
| `libpng` | PNG encoding |
| `xkbcommon` | XKB keyboard mapping on Wayland (optional) |

A terminal emulator with **Kitty Graphics Protocol** support is required for display (e.g. [kitty](https://sw.kovidgoyal.net/kitty/), [konsole](https://konsole.kde.org/), [wezterm](https://wezfurlong.org/wezterm/), [ghostty](https://ghostty.org/)).

## Building

### CMake

```sh
cmake -S. -B build -G Ninja
cmake --build build
```

Install:

```sh
cmake --install build
```

## Usage

```
spice-kitten -a <addr> -p <port> -v -m -c -h

  -a <addr>  IPv4 address (default: 127.0.0.1)
  -p <port>  SPICE port  (default: 5900)
  -v         Enable logging to /tmp/spice_client.log
  -m         Hide mouse cursor
  -c         Do not clear screen (use ANSI escape instead)
  -h         Print help
```

**Quit:** `Alt+Q`

## QEMU configuration

spice-kitten works best with `-vga virtio`. The `-vga qxl` driver uses `SPICE_CLIP_TYPE_RECTS`, which is not supported — artefacts or missing regions may appear.

Example:

```sh
qemu-system-x86_64 \
  -vga virtio \
  -spice port=5900,disable-ticketing=on \
  ...
```

## Project structure

```
spice_kitten.c            Main application (X11 input backend)
libspice/                 SPICE client library (static)
  include/libspice.h      Public API
  sp_proto.c              Protocol: channel init, main/display/input loops
  sp_screen.c             Screen: BMP→PNG→shm→Kitty output
  sp_utils.c              Utilities: init, connect, password, scaling
pkg/gentoo/               Gentoo ebuild
```

## License

[BSD 2-Clause](LICENSE) © 2026 nEMU
