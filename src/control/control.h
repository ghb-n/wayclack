#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "../app/app.h"

namespace WayClack {

// WayClack can run in two processes: a background daemon (tray) and a terminal
// UI. To avoid two audio listeners competing for the same keyboard device, the
// daemon hosts a small line-based control protocol on a Unix socket, and the
// TUI connects to it and drives the *running* instance.
//
// Socket location: $XDG_RUNTIME_DIR/wayclack.sock (falls back to /tmp).
std::string controlSocketPath();

// ---------------------------------------------------------------------------
// Server side (the daemon)
// ---------------------------------------------------------------------------
class ControlServer {
public:
  ControlServer() = default;
  ~ControlServer();

  ControlServer(const ControlServer &) = delete;
  ControlServer &operator=(const ControlServer &) = delete;

  // Bind the socket and start accepting clients. Returns false if the socket
  // is already in use (another daemon) or cannot be created.
  // `quitFlag` is set to true when a client asks the daemon to exit.
  bool start(App &app, std::atomic<bool> *quitFlag = nullptr);
  void stop();

  bool running() const { return fd_ >= 0; }

  // Service pending client requests. Call from the main loop; non-blocking.
  void poll();

  // Queue a key event to be streamed to connected clients.
  void pushKeyEvent(const std::string &name, const std::string &sound,
                    bool hadSound);

private:
  struct Client {
    int fd = -1;
    std::string in; // Partial input line buffer.
  };

  void handleLine(Client &client, const std::string &line);
  void sendState(Client &client);
  void broadcastEvent(const std::string &name, const std::string &sound,
                      bool hadSound);
  void dropClient(Client &client);

  App *app_ = nullptr;
  int fd_ = -1;
  std::string path_;
  std::atomic<bool> *quitFlag_ = nullptr;
  std::vector<Client> clients_;
};

// ---------------------------------------------------------------------------
// Client side (the TUI)
// ---------------------------------------------------------------------------
// A thin client that talks to a running ControlServer.
class ControlClient {
public:
  ControlClient() = default;
  ~ControlClient();

  ControlClient(const ControlClient &) = delete;
  ControlClient &operator=(const ControlClient &) = delete;

  // Connect to a running daemon. Returns false if none is listening.
  bool connect();

  bool connected() const { return fd_ >= 0; }
  void close();

  // State cached from the server's "state" replies.
  struct State {
    int volume = 100;
    bool muted = false;
    bool listening = false;
    int selectedPack = -1;
    std::string device;
    std::string deviceName;
    std::string status;
    std::vector<std::string> packs;
  };

  const State &state() const { return state_; }

  // Commands (fire-and-forget).
  void volumeUp();
  void volumeDown();
  void toggleMute();
  void nextPack();
  void prevPack();
  void selectPack(int index);
  void quitServer(); // Ask the daemon to exit (tray Quit).

  struct KeyEvent {
    std::string name;
    std::string sound;
    bool hadSound = false;
  };

  // Non-blocking read of server messages; updates state() and fills
  // `eventsOut` with any streamed key events.
  void poll(std::vector<KeyEvent> *eventsOut);

private:
  void sendLine(const std::string &line);

  int fd_ = -1;
  std::string buffer_;
  State state_;
};

} // namespace WayClack
