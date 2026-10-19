#pragma once
#include "../audio_driver/audio.h"
#include "../discovery/discovery.h"
#include "../keyboard/keyboard.h"
#include <deque>
#include <string>
#include <termios.h>
#include <vector>

namespace WayClack {

// Configuration passed into the TUI at start-up.
struct TuiOptions {
  std::string packsDir;             // Directory to scan for packs.
  std::string devicePath;           // evdev node (may be empty -> autodetect).
  bool startMuted = false;          // Start with audio muted.
};

// TUI renders an interactive dashboard in the terminal and drives the
// Keyboard listener.
//
// Layout:
//   +--------------------------------------------------------------+
//   | WAYCLACK  <status>                                           |
//   +---------------------------+----------------------------------+
//   | Status panel              | Pack list                        |
//   +---------------------------+----------------------------------+
//   | Volume bar                                                   |
//   +--------------------------------------------------------------+
//   | Keystroke feed                                               |
//   +--------------------------------------------------------------+
//   | Controls: ...                                                |
//   +--------------------------------------------------------------+
class CLI {
public:
  CLI() = default;
  ~CLI();

  CLI(const CLI &) = delete;
  CLI &operator=(const CLI &) = delete;

  // Run the dashboard until the user quits. Returns a process exit code.
  int run(const TuiOptions &options);

private:
  // --- Rendering ---
  void render();
  std::string buildFrame() const;
  void drawVolumeBar(std::string &out, int width) const;
  void moveCursorToBottom();

  // --- Input handling ---
  bool handleKey(int key);
  void loadPack(size_t index, bool resetVolume);

  // --- Terminal setup / teardown ---
  bool enterRawMode();
  void leaveRawMode();

  // --- Data helpers ---
  std::vector<std::string> collectPackSounds(const std::string &configPath,
                                             int &outVolume);

  // --- State ---
  AudioPlayer *player_ = nullptr;
  Keyboard *keyboard_ = nullptr;

  std::vector<PackInfo> packs_;
  int selectedPack_ = -1;

  std::deque<std::string> feed_;
  std::string statusMessage_;
  std::string device_;
  std::string deviceName_;
  bool listenerOk_ = false;
  bool running_ = true;

  // Terminal state.
  bool rawMode_ = false;
  int termFd_ = 0;
  termios savedTermios_{};
  int termWidth_ = 80;
  int termHeight_ = 24;
};

} // namespace WayClack
