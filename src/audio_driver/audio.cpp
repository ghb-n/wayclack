#include "audio.h"
#include <iostream>
using namespace std;
namespace WayClack {
AudioPlayer::AudioPlayer () {}

AudioPlayer::~AudioPlayer () {
  cleanup();
}

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

    isInitialized = true;
    return true;
}

bool AudioPlayer::loadSound(const std::string& filePath) {
  if (!isInitialized) return false;

  if (soundEffect != nullptr) {
    Mix_FreeChunk(soundEffect);
  }

  soundEffect = Mix_LoadWAV(filePath.c_str());
  if (soundEffect == nullptr) {
    cerr << "Failed to load WAV: " << Mix_GetError() << "\n";
    return false;
  }

  return true;
}

void AudioPlayer::playSound() {
  if (soundEffect != nullptr) {
    Mix_PlayChannel(-1, soundEffect, 0);
  }
}

void AudioPlayer::cleanup() {
  if (soundEffect != nullptr) {
    Mix_FreeChunk(soundEffect);
    soundEffect = nullptr;
  }
  if(isInitialized) {
    Mix_CloseAudio();
    SDL_Quit();
    isInitialized = false;
  }
}

} // namespace WayClack
