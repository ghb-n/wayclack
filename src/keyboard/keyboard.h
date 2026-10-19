#pragma once

#include "../audio_driver/audio.h"
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/input.h>
#include <stdexcept>
#include <unistd.h>
#include <unordered_map>

namespace WayClack {
class Keyboard {
public:
  void listenToInput(const char *devicePath,
                     const std::unordered_map<int, std::string> &keySounds,
                     WayClack::AudioPlayer &player);
};
} // namespace WayClack
