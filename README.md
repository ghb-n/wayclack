# WayClack - Inspired by MechVibes

A lightweight, linux-native mechanical keyboard sound emulator designed for modern WayLand environments.

WayClack listens directly to raw Kernel input events ('evdev'), parses custom configuration profiles, and triggers low-latency mechanical switch audio samples on every keystroke.

---

## Features

- **Low-Latency Input Listening**: Bypasses display server restrictions by interacting directly with the Linux input subsystem (`/dev/input/event*`).
- **JSON Configuration**: Powered by `nlohmann/json` for clean, flexible sound theme and key-mapping configurations.
- **Modular Architecture**: Built with modern C++ principles (RAII, class encapsulation, and separation of concerns).

---

## Change Logs

- [Change Logs](changelog.txt)

## License

This project is open-source and avaliable under the [MIT License](LICENSE)
