#!/usr/bin/env bash
# Install WayClack for the current user (no root required).
#
#   scripts/install.sh              # build + install binary, icon, launcher
#   scripts/install.sh --autostart  # also start the tray service at login
#   scripts/install.sh --uninstall  # remove everything this script installed
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PREFIX="${WAYCLACK_PREFIX:-$HOME/.local}"
BINDIR="$PREFIX/bin"
ICONDIR="$PREFIX/share/icons/hicolor/512x512/apps"
APPDIR="$PREFIX/share/applications"
AUTOSTARTDIR="${XDG_CONFIG_HOME:-$HOME/.config}/autostart"

AUTOSTART=0
UNINSTALL=0
for arg in "$@"; do
  case "$arg" in
    --autostart) AUTOSTART=1 ;;
    --uninstall) UNINSTALL=1 ;;
    -h|--help) sed -n '2,10p' "$0"; exit 0 ;;
    *) echo "Unknown option: $arg" >&2; exit 2 ;;
  esac
done

if [[ "$UNINSTALL" -eq 1 ]]; then
  echo "==> Removing WayClack"
  # Stop a running daemon politely.
  pkill -x wayclack 2>/dev/null || true
  rm -f "$BINDIR/wayclack" \
        "$ICONDIR/wayclack.png" \
        "$APPDIR/wayclack.desktop" \
        "$AUTOSTARTDIR/wayclack.desktop"
  gtk-update-icon-cache -f -t "$PREFIX/share/icons/hicolor" 2>/dev/null || true
  update-desktop-database "$APPDIR" 2>/dev/null || true
  echo "    done."
  exit 0
fi

echo "==> Building (Release)"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build -j"$(nproc)" >/dev/null

echo "==> Installing binary to $BINDIR/wayclack"
install -d "$BINDIR" "$ICONDIR" "$APPDIR"
install -m0755 build/wayclack "$BINDIR/wayclack"

echo "==> Installing icon (wayclack)"
install -m0644 docs/logo.png "$ICONDIR/wayclack.png"
gtk-update-icon-cache -f -t "$PREFIX/share/icons/hicolor" 2>/dev/null || true

echo "==> Installing desktop launcher"
install -m0644 packaging/wayclack.desktop "$APPDIR/wayclack.desktop"
# Point the launcher at the installed binary explicitly.
sed -i "s|^Exec=.*|Exec=$BINDIR/wayclack|" "$APPDIR/wayclack.desktop"
update-desktop-database "$APPDIR" 2>/dev/null || true

if [[ "$AUTOSTART" -eq 1 ]]; then
  echo "==> Enabling autostart (tray service at login)"
  install -d "$AUTOSTARTDIR"
  install -m0644 packaging/wayclack-autostart.desktop "$AUTOSTARTDIR/wayclack.desktop"
  sed -i "s|^Exec=.*|Exec=$BINDIR/wayclack --daemon|" \
    "$AUTOSTARTDIR/wayclack.desktop"
fi

echo
echo "Installed. Next steps:"
echo "  * Ensure you are in the 'input' group:  sudo usermod -aG input \"\$USER\""
echo "  * Make sure $BINDIR is in your PATH."
if [[ "$AUTOSTART" -eq 1 ]]; then
  echo "  * The tray service will start at your next login."
  echo "    Start it now with:  $BINDIR/wayclack --daemon &"
else
  echo "  * Start the tray service:  wayclack --daemon &"
  echo "    Or re-run with --autostart to launch it at login."
fi
