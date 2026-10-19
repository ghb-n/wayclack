#pragma once

#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/input.h>
#include <stdexcept>
#include <unistd.h>

namespace WayClack {
class Keyboard {
public:
  void listenToInput(const char *devicePath);
};
} // namespace WayClack
