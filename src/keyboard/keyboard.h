#pragma once

#include "../audio_driver/audio.h"
#include <atomic>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <iostream>
#include <linux/input.h>
#include <mutex>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include <unordered_map>

namespace WayClack {

// A single decoded keystroke, surfaced to the UI.
struct KeyEvent {
  int code = 0;
  std::string name;
  std::string sound;
  bool hadSound = false;
};

// Keyboard reads raw evdev events on a background thread and triggers audio.
//
// The listener owns its own thread so the main thread is free to drive the
// interactive UI. start()/stop() are safe to call repeatedly; stop() joins the
// reader thread and closes the device.
class Keyboard {
public:
  Keyboard() = default;
  ~Keyboard();

  Keyboard(const Keyboard &) = delete;
  Keyboard &operator=(const Keyboard &) = delete;

  // Configure the active pack. The map is keyed by Linux key code.
  void setKeySounds(const std::unordered_map<int, std::string> &keySounds);

  // Open the device and spawn the reader thread. Returns false on failure
  // (device missing or unreadable) without spawning anything.
  bool start(const std::string &devicePath, AudioPlayer &player);

  // Signal the reader thread to exit and join it.
  void stop();

  bool running() const { return running_.load(); }
  std::string devicePath() const { return devicePath_; }

  // Copy up to `max` recent events (oldest first) and clear the buffer.
  std::deque<KeyEvent> drainEvents(size_t max);

  // Human-readable name for a Linux key code, e.g. KEY_A -> "A".
  static std::string keyName(int code);

private:
  void readerLoop();

  std::unordered_map<int, std::string> keySounds_;
  std::string devicePath_;
  std::atomic<bool> running_{false};
  std::atomic<int> fd_{-1};
  std::thread thread_;
  AudioPlayer *player_ = nullptr;

  std::mutex eventsMutex_;
  std::deque<KeyEvent> events_;
};

} // namespace WayClack
