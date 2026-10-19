#include "keyboard.h"
#include <fcntl.h>
#include <stdexcept>

void WayClack::Keyboard::listenToInput(const char *devicePath) {
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

    if (ev.type == EV_KEY) {
      if (ev.value == 1) {
        std::cout << "Key DOWN code: " << ev.code << "\n";
      } else if (ev.value == 0) {
        std::cout << "Key UP Code: " << ev.code << "\n";
      }
    }
  }

  close(fd);
}
