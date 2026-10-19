# WayClack v1.0.0

The first stable release of WayClack — a lightweight, Linux-native mechanical
keyboard sound emulator for Wayland.

## Highlights

### Interactive TUI
A fast terminal dashboard built in plain C++ (ANSI escapes + raw `termios`),
with **no extra dependencies**:

- live volume bar with mute toggle
- sound-pack switcher (next / previous / reload)
- real-time keystroke feed
- status panel showing the active device, pack, and sound state

Controls: `+`/`-` volume · `m` mute · `n`/`p` pack · `r` reload · `q` quit.

### Smart keyboard autodetection
Every input node is scored, so WayClack finds the right keyboard automatically:

- **built-in laptop keyboards** (`i8042`/`serio`, e.g. *"AT Translated Set 2
  keyboard"*) are detected like USB boards;
- a **USB mouse exposing a HID keyboard interface** is down-ranked;
- button/media-only nodes (Power Button, Video Bus, Intel HID events, …) are
  rejected;
- stable `/dev/input/by-id` and `by-path` links are preferred when available.

Inspect the ranking with:

```sh
wayclack --list-devices
```

### Real-time, low-latency audio
The keyboard listener runs on a **background thread** while the UI stays
interactive. Samples are decoded once and **cached per pack** — no disk reads on
keystroke, and no leaks. Volume and mute apply instantly.

### Runs as a background app with a panel icon
Use `--daemon` to run without a terminal and show a **system-tray icon** with a
menu to mute, change volume, switch sound packs, open the TUI, or quit. The tray
is a StatusNotifierItem implemented **directly on D-Bus** — no Qt, GTK or
appindicator dependency. `scripts/install.sh --autostart` starts it at login.

*Open TUI* attaches a terminal dashboard to the running daemon over a Unix
socket (`--attach`), so there is exactly **one** audio listener: closing the
window detaches and the tray keeps running. The tray's *Quit* stops everything.

### Removed hardcoded paths
Device and pack locations are now configurable (`--device`, `--packs`) with
sensible defaults, replacing the previous hardcoded personal paths.

## Install

```sh
# Dependencies (Debian/Ubuntu)
sudo apt install libsdl2-dev libsdl2-mixer-dev nlohmann-json3-dev libdbus-1-dev cmake g++

# Build from the source archive
tar -xzf wayclack-1.0.0-src.tar.gz
cd wayclack-1.0.0
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# Run (terminal UI)
./build/wayclack --list-devices
./build/wayclack --packs ~/mechvibes_custom

# Or install per-user (add --autostart to start the tray at login)
scripts/install.sh
```

> Reading `/dev/input/event*` requires membership in the `input` group:
> `sudo usermod -aG input "$USER"` (then re-login).

## Full changelog

See [`changelog.txt`](changelog.txt).

## Assets

- `wayclack-1.0.0-linux-<arch>.tar.gz` — prebuilt binary bundle
- `wayclack-1.0.0-src.tar.gz` — source archive
- `SHA256SUMS` — checksums for the above
