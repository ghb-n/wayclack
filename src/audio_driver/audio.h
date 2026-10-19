#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <string>

namespace WayClack {
class AudioPlayer {
public:
  AudioPlayer();
  ~AudioPlayer();

  bool init();
  bool loadSound(const std::string &filePath);
  void playSound(const std::string &filePath);
  void cleanup();

private:
  Mix_Chunk *soundEffect = nullptr;
  bool isInitialized = false;
};
} // namespace WayClack
