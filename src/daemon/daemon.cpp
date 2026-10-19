#include "daemon.h"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

#include "../control/control.h"

#ifdef WAYCLACK_HAVE_DBUS
#include "../tray/tray.h"
#endif

using namespace std;

namespace WayClack {
namespace {

std::atomic<bool> g_quit{false};
std::atomic<bool> g_openTui{false};

void handleSignal(int) { g_quit.store(true); }

// Find a terminal emulator to host the TUI, in order of preference.
string findTerminal() {
  const char *candidates[] = {"konsole", "kitty", "alacritty", "foot",
                              "gnome-terminal", "xterm"};
  for (const char *c : candidates) {
    string path = string("/usr/bin/") + c;
    if (access(path.c_str(), X_OK) == 0)
      return c;
  }
  return "";
}

// Launch the TUI in a new terminal window. The TUI attaches to this running
// daemon over the control socket, so it does NOT start a second audio
// listener: closing it leaves the tray running.
void openTui(const string &argv0) {
  string term = findTerminal();
  if (term.empty()) {
    cerr << "No terminal emulator found to open the TUI.\n";
    return;
  }

  string exe = argv0;
  if (exe.find('/') == string::npos) {
    // Resolve a bare name to an absolute path so the terminal finds it.
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
      buf[n] = '\0';
      exe = buf;
    }
  }

  // Force attach mode: the TUI connects to the running daemon.
  vector<string> args = {term, "-e", exe, "--tui", "--attach"};

  pid_t pid = fork();
  if (pid == 0) {
    // Child: new session so closing the terminal does not signal us.
    setsid();
    vector<char *> cargv;
    cargv.reserve(args.size() + 1);
    for (auto &a : args)
      cargv.push_back(const_cast<char *>(a.c_str()));
    cargv.push_back(nullptr);
    execvp(cargv[0], cargv.data());
    _exit(127);
  }
  // Parent: reap later; best-effort avoid a zombie if the child exits fast.
  if (pid > 0)
    (void)waitpid(pid, nullptr, WNOHANG);
}

} // namespace

int runDaemon(const AppOptions &options) {
  App app;
  if (!app.start(options)) {
    cerr << "WayClack: " << app.reason() << "\n";
    return 1;
  }

  // Clean shutdown on SIGINT/SIGTERM (e.g. from `systemctl --user stop`).
  struct sigaction sa {};
  sa.sa_handler = handleSignal;
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
  signal(SIGPIPE, SIG_IGN);

  // Host the control socket so a TUI can attach to this instance.
  ControlServer control;
  if (!control.start(app, &g_quit))
    cerr << "WayClack: control socket unavailable (another daemon running?).\n"
         << "          The tray works, but 'Open TUI' may start a new "
            "instance.\n";

#ifdef WAYCLACK_HAVE_DBUS
  string argv0;
  {
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
      buf[n] = '\0';
      argv0 = buf;
    }
  }

  TrayIcon tray;
  TrayCallbacks cb;
  cb.onMuteToggle = [&] { app.toggleMute(); };
  cb.onVolumeUp = [&] { app.volumeUp(); };
  cb.onVolumeDown = [&] { app.volumeDown(); };
  cb.onNextPack = [&] { app.nextPack(); };
  cb.onPrevPack = [&] { app.prevPack(); };
  cb.onOpenTui = [&] { g_openTui.store(true); };
  cb.onQuit = [&] { g_quit.store(true); };

  bool trayReady = tray.start("wayclack", cb);
  if (!trayReady) {
    cerr << "WayClack: could not register the tray icon (no session bus?).\n"
         << "          Continuing without a tray; send SIGTERM to stop.\n";
  } else {
    cout << "WayClack is running in the panel. Quit from the tray menu.\n";
  }
  string lastTitle;
#else
  cout << "WayClack daemon running (built without D-Bus tray support).\n"
       << "Attach a TUI with: wayclack --attach   (or send SIGTERM to stop)\n";
#endif

  // Main loop: service the tray on a worker thread so we can refresh the title,
  // pump the control socket, and honour signals on this thread.
  thread trayThread;
#ifdef WAYCLACK_HAVE_DBUS
  if (trayReady)
    trayThread = thread([&] { tray.run(); });
#endif

  while (!g_quit.load() && !app.quitRequested()) {
    // Service attached TUI clients and forward keyboard events to them.
    control.poll();
    auto events = app.keyboard().drainEvents(64);
    for (auto &e : events)
      control.pushKeyEvent(e.name, e.sound, e.hadSound);

#ifdef WAYCLACK_HAVE_DBUS
    if (g_openTui.exchange(false))
      openTui(argv0);

    char title[256];
    snprintf(title, sizeof(title), "WayClack - %s%s %d%%",
             app.activePackName().c_str(), app.muted() ? " [muted]" : "",
             app.volume());
    if (title != lastTitle) {
      lastTitle = title;
      tray.setTitle(title);
    }
#endif
    usleep(50 * 1000);
  }

#ifdef WAYCLACK_HAVE_DBUS
  tray.stop();
  if (trayThread.joinable())
    trayThread.join();
#endif
  control.stop();

  app.stop();
  cout << "WayClack stopped.\n";
  return 0;
}

} // namespace WayClack
