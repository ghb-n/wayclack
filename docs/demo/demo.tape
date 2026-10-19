# WayClack demo — reproducible terminal recording.
#
# Renders docs/demo.gif with VHS (https://github.com/charmbracelet/vhs):
#
#     vhs docs/demo/demo.tape
#
# VHS drives a real terminal, so the recording shows live audio-keyboard
# reaction. Build the binary first (`cmake --build build`) and point
# `--packs` at the directory holding your sound packs.

Output docs/demo.gif

Set Shell "bash"
Set FontSize 14
Set Width 900
Set Height 560
Set Padding 18
Set Margin 0
Set BorderRadius 10
Set Theme "Catppuccin Mocha"
Set TypingSpeed 60ms
Set Framerate 30

# Show where the binary and packs live (edit paths for your setup).
Env WAYCLACK_PACKS "$HOME/mechvibes_custom"

# --- Intro ---------------------------------------------------------------
Type "./build/wayclack --packs $WAYCLACK_PACKS"
Enter
Sleep 2.5s

# --- Volume up -----------------------------------------------------------
Type "+++"
Sleep 1s

# --- Mute, then unmute ---------------------------------------------------
Type "m"
Sleep 1.2s
Type "m"
Sleep 1s

# --- Cycle through sound packs ------------------------------------------
Type "n"
Sleep 1.2s
Type "n"
Sleep 1.2s
Type "p"
Sleep 1s

# --- Quit ----------------------------------------------------------------
Type "q"
Sleep 1s
