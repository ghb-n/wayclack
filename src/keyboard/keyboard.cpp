#include "keyboard.h"
#include <fcntl.h>
#include <stdexcept>

void WayClack::Keyboard::listenToInput(
    const char *devicePath,
    const std::unordered_map<int, std::string> &keySounds,
    WayClack::AudioPlayer &player) {
  int fd = open(devicePath, O_RDONLY);
  if (fd < 0) {
    throw std::runtime_error(
        "Failed to open device: " + std::string(devicePath) +
        " ( Are you in the 'input' group or running as root?)");
  }

  std::cout << "Listening to: " << devicePath << "....\n";

  struct input_event ev;

  while (true) {
    ssize_t bytesRead = read(fd, &ev, sizeof(ev));
    if (bytesRead < (ssize_t)sizeof(ev)) {
      // Handle the error
      continue;
    }

    if (ev.type == EV_KEY && ev.value == 1) {
      int keyCode = ev.code;

      auto it = keySounds.find(keyCode);
      if (it != keySounds.end()) {
        std::string soundFile = it->second;

        player.playSound(soundFile);
        std::cout << "Key " << ev.code << " Pressed -> Playing: " << soundFile
                  << "\n";
      }
    }
  }

  close(fd);
}
