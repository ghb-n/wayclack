#pragma once
#include "../audio_driver/audio.h"
#include "../app/app.h"
#include "../control/control.h"
#include "../discovery/discovery.h"
#include "../keyboard/keyboard.h"
#include <deque>
#include <string>
#include <termios.h>
#include <vector>

namespace WayClack {

// The TUI takes the same start-up options as every other front-end.
using TuiOptions = AppOptions;

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

  // Run as a thin client attached to a running daemon: same dashboard, but it
  // drives the existing instance over the control socket instead of starting a
  // second audio listener. Returns non-zero if no daemon is running.
  int runAttached();

private:
  // --- Rendering ---
  void render();
  std::string buildFrame() const;
  void drawVolumeBar(std::string &out, int width) const;
  void moveCursorToBottom();

  // --- Input handling ---
  bool handleKey(int key);
  void loadPack(size_t index, bool resetVolume);

  // Attached-mode input handling and state refresh.
  bool handleAttachedKey(int key);
  void refreshAttachedState();

  // --- Terminal setup / teardown ---
  bool enterRawMode();
  void leaveRawMode();

  // --- Data helpers ---
  std::vector<std::string> collectPackSounds(const std::string &configPath,
                                             int &outVolume);

  // --- State ---
  AudioPlayer *player_ = nullptr;
  Keyboard *keyboard_ = nullptr;
  ControlClient *client_ = nullptr; // Set in attached mode.
  bool attached_ = false;

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
