#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "../audio_driver/audio.h"
#include "../discovery/discovery.h"
#include "../keyboard/keyboard.h"

namespace WayClack {

// Options shared by every front-end (TUI and tray daemon).
struct AppOptions {
  std::string packsDir = "~/mechvibes_custom"; // Directory to scan for packs.
  std::string devicePath;                      // evdev node ("" -> autodetect).
  bool startMuted = false;                     // Start with audio muted.
};

// App is the headless core of WayClack: it owns the audio device and the
// keyboard listener and exposes the operations a front-end needs (volume,
// mute, pack switching). Both the terminal UI and the system-tray daemon drive
// the same instance, so the behaviour stays identical in either mode.
class App {
public:
  App() = default;
  ~App();

  App(const App &) = delete;
  App &operator=(const App &) = delete;

  // Initialise audio, discover packs and the keyboard device, load the first
  // pack, and start listening. Returns false (with reason()) on failure.
  bool start(const AppOptions &options);

  // Stop listening and release resources.
  void stop();

  // --- Controls (safe to call from the tray/app thread) ---
  void volumeUp(int step = 5);
  void volumeDown(int step = 5);
  void toggleMute();
  void nextPack();
  void prevPack();
  void selectPack(int index);

  // Set when a front-end (e.g. a TUI client, or a tray menu entry) asks the
  // running instance to exit. The daemon loop watches this.
  void requestQuit() { quitRequested_.store(true); }
  bool quitRequested() const { return quitRequested_.load(); }

  // --- Introspection ---
  int volume() const { return player_.volume(); }
  bool muted() const { return player_.muted(); }
  bool listening() const { return keyboard_.running(); }

  const std::vector<PackInfo> &packs() const { return packs_; }
  int selectedPack() const { return selectedPack_; }
  std::string activePackName() const;

  const std::string &device() const { return device_; }
  const std::string &deviceName() const { return deviceName_; }

  const std::string &status() const { return status_; }
  const std::string &reason() const { return reason_; }

  // Shared accessors so a front-end can render directly from the same objects
  // the core drives (the TUI needs live volume/mute and the event feed).
  AudioPlayer &audio() { return player_; }
  Keyboard &keyboard() { return keyboard_; }

private:
  void loadPack(size_t index, bool resetVolume);

  AudioPlayer player_;
  Keyboard keyboard_;
  std::vector<PackInfo> packs_;
  int selectedPack_ = -1;

  std::string device_;
  std::string deviceName_;
  std::string status_;
  std::string reason_;

  std::atomic<bool> quitRequested_{false};

  mutable std::mutex mutex_;
};

} // namespace WayClack
