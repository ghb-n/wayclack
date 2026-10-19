#include "mainwindow.h"
#include "cli/cli.h"
#include "discovery/discovery.h"

#include <cstring>
#include <iostream>
#include <string>

#ifndef WAYCLACK_VERSION
#define WAYCLACK_VERSION "dev"
#endif

using namespace std;

namespace {

void printUsage(const char *argv0) {
  cout << "WayClack " << WAYCLACK_VERSION
       << " - mechanical keyboard sound emulator (TUI)\n\n"
       << "Usage: " << argv0 << " [options]\n\n"
       << "Options:\n"
       << "  -p, --packs <dir>    Directory containing sound packs\n"
       << "                       (default: ~/mechvibes_custom)\n"
       << "  -d, --device <path>  evdev device, e.g. /dev/input/event0\n"
       << "                       (default: autodetect)\n"
       << "  -l, --list-devices   List detected keyboard devices and exit\n"
       << "  -m, --mute           Start muted\n"
       << "  -v, --version        Show version and exit\n"
       << "  -h, --help           Show this help\n\n"
       << "Controls: [+/-] volume  [m] mute  [n/p] pack  [r] reload  [q] quit\n";
}

void printDevices() {
  auto devices = WayClack::Discovery::listKeyboardCandidates();
  if (devices.empty()) {
    cout << "No keyboard-like devices found.\n";
    return;
  }
  cout << "Detected keyboard-like devices (best first):\n\n";
  for (const auto &d : devices) {
    cout << "  score " << d.score << "  " << d.path << "\n"
         << "             name: " << d.name << "\n";
    if (!d.stablePath.empty())
      cout << "             link: " << d.stablePath << "\n";
  }
  cout << "\nA device with a positive score is a good candidate.\n";
}

} // namespace

int main(int argc, char **argv) {
  WayClack::TuiOptions options;
  options.packsDir = "~/mechvibes_custom";

  for (int i = 1; i < argc; ++i) {
    string arg = argv[i];
    auto next = [&](string &out) {
      if (i + 1 < argc)
        out = argv[++i];
    };

    if (arg == "-h" || arg == "--help") {
      printUsage(argv[0]);
      return 0;
    } else if (arg == "-v" || arg == "--version") {
      cout << "WayClack " << WAYCLACK_VERSION << "\n";
      return 0;
    } else if (arg == "-l" || arg == "--list-devices") {
      printDevices();
      return 0;
    } else if (arg == "-p" || arg == "--packs") {
      next(options.packsDir);
    } else if (arg == "-d" || arg == "--device") {
      next(options.devicePath);
    } else if (arg == "-m" || arg == "--mute") {
      options.startMuted = true;
    } else {
      cerr << "Unknown option: " << arg << "\n\n";
      printUsage(argv[0]);
      return 2;
    }
  }

  WayClack::CLI cli;
  try {
    return cli.run(options);
  } catch (const exception &e) {
    cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}
