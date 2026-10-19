#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace WayClack {

// AudioPlayer owns the SDL_mixer audio device and the decoded sound samples
// for the currently loaded pack.
//
// Samples are cached by file path the first time they are requested, so a
// keystroke never touches the disk (the previous implementation called
// Mix_LoadWAV on every key press and leaked the resulting chunk).
class AudioPlayer {
public:
  AudioPlayer();
  ~AudioPlayer();

  AudioPlayer(const AudioPlayer &) = delete;
  AudioPlayer &operator=(const AudioPlayer &) = delete;

  bool init();
  void cleanup();

  // Pre-decode a list of sample paths (relative to basePath).
  void preload(const std::string &basePath,
               const std::vector<std::string> &sounds);

  // Play a sample. Paths are resolved relative to the pack base path.
  void playSound(const std::string &filePath);

  // Volume is expressed as a percentage (0-100). Muting is independent of the
  // stored volume so unmuting restores the previous level.
  void setVolume(int percent);
  int volume() const;
  void setMuted(bool muted);
  bool muted() const;

  // Drop all cached samples (used when switching packs).
  void clearCache();

private:
  std::string resolve(const std::string &filePath) const;

  mutable std::mutex mutex_;
  std::unordered_map<std::string, Mix_Chunk *> cache_;
  std::string basePath_;
  int volumePercent_ = 100;
  bool muted_ = false;
  bool isInitialized = false;
};

} // namespace WayClack
