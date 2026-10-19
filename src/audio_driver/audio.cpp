#include "audio.h"
#include <iostream>
#include <vector>

using namespace std;

namespace WayClack {

AudioPlayer::AudioPlayer() {}

AudioPlayer::~AudioPlayer() { cleanup(); }

bool AudioPlayer::init() {
  if (SDL_Init(SDL_INIT_AUDIO) < 0) {
    cerr << "SDL Init Error: " << SDL_GetError() << "\n";
    return false;
  }

  if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
    cerr << "SDL_mixer Init Error: " << Mix_GetError() << "\n";
    SDL_Quit();
    return false;
  }

  Mix_AllocateChannels(32);
  // Pre-apply the default volume/mute state.
  int vol = (volumePercent_ * MIX_MAX_VOLUME) / 100;
  Mix_Volume(-1, muted_ ? 0 : vol);
  isInitialized = true;
  return true;
}

std::string AudioPlayer::resolve(const std::string &filePath) const {
  if (filePath.empty())
    return filePath;
  // Already absolute.
  if (filePath.front() == '/')
    return filePath;
  if (basePath_.empty())
    return filePath;
  if (basePath_.back() == '/')
    return basePath_ + filePath;
  return basePath_ + "/" + filePath;
}

void AudioPlayer::preload(const std::string &basePath,
                          const std::vector<std::string> &sounds) {
  std::lock_guard<std::mutex> lock(mutex_);
  basePath_ = basePath;
  for (const auto &s : sounds) {
    if (cache_.count(s))
      continue;
    std::string full = resolve(s);
    Mix_Chunk *chunk = Mix_LoadWAV(full.c_str());
    if (chunk == nullptr) {
      // Not fatal: a pack may legitimately reference a missing sample.
      continue;
    }
    cache_[s] = chunk;
  }
}

void AudioPlayer::playSound(const std::string &filePath) {
  Mix_Chunk *chunk = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cache_.find(filePath);
    if (it != cache_.end()) {
      chunk = it->second;
    } else if (isInitialized) {
      // Lazy-load and cache on miss.
      std::string full = resolve(filePath);
      chunk = Mix_LoadWAV(full.c_str());
      if (chunk != nullptr)
        cache_[filePath] = chunk;
    }
  }

  if (chunk == nullptr)
    return;
  Mix_PlayChannel(-1, chunk, 0);
}

void AudioPlayer::setVolume(int percent) {
  if (percent < 0)
    percent = 0;
  if (percent > 100)
    percent = 100;
  std::lock_guard<std::mutex> lock(mutex_);
  volumePercent_ = percent;
  Mix_Volume(-1, muted_ ? 0 : (percent * MIX_MAX_VOLUME) / 100);
}

int AudioPlayer::volume() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return volumePercent_;
}

void AudioPlayer::setMuted(bool muted) {
  std::lock_guard<std::mutex> lock(mutex_);
  muted_ = muted;
  Mix_Volume(-1, muted_ ? 0 : (volumePercent_ * MIX_MAX_VOLUME) / 100);
}

bool AudioPlayer::muted() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return muted_;
}

void AudioPlayer::clearCache() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto &[path, chunk] : cache_) {
    if (chunk != nullptr)
      Mix_FreeChunk(chunk);
  }
  cache_.clear();
}

void AudioPlayer::cleanup() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &[path, chunk] : cache_) {
      if (chunk != nullptr)
        Mix_FreeChunk(chunk);
    }
    cache_.clear();
  }
  if (isInitialized) {
    Mix_CloseAudio();
    SDL_Quit();
    isInitialized = false;
  }
}

} // namespace WayClack
