#pragma once
#include <string>
#include <vector>

namespace WayClack {

// A discovered MechVibes-style sound pack.
struct PackInfo {
  std::string name;   // Display name (config "name", or directory name).
  std::string id;     // config "id", if present.
  std::string dir;    // Absolute path to the pack directory.
  std::string config; // Absolute path to config.json.
};

// A candidate input device considered during keyboard autodetection.
struct DeviceInfo {
  std::string path;        // e.g. /dev/input/event0
  std::string stablePath;  // by-id/by-path symlink, if any (preferred).
  std::string name;        // sysfs device name.
  int score = 0;           // Ranking score; higher is more keyboard-like.
};

namespace Discovery {

// Scan `baseDir` for immediate subdirectories containing a config.json.
std::vector<PackInfo> findPacks(const std::string &baseDir);

// Best-effort autodetection of a keyboard evdev node.
//
// Uses a scoring heuristic rather than first-match so that:
//   * built-in laptop keyboards (e.g. "AT Translated Set 2 keyboard",
//     exposed via i8042/serio) are found just like USB keyboards;
//   * a USB mouse that exposes a secondary HID "Keyboard" interface is
//     down-ranked in favour of the real keyboard;
//   * media/button-only nodes (Power Button, Video Bus, Intel HID events...)
//     are rejected.
//
// Returns an empty string if nothing suitable is found.
std::string findKeyboardDevice();

// Like findKeyboardDevice(), but also reports the chosen device's details.
// `info` is populated only when a device is found.
std::string findKeyboardDevice(DeviceInfo *info);

// Enumerate all candidate keyboard devices with their scores, best first.
// Useful for diagnostics / a future --list-devices flag.
std::vector<DeviceInfo> listKeyboardCandidates();

// Expand a leading "~" to $HOME.
std::string expandUser(const std::string &path);

} // namespace Discovery
} // namespace WayClack
