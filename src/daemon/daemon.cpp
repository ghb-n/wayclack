#include "daemon.h"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

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

// Launch the TUI in a new terminal window. Detached: we do not wait for it.
void openTui(const string &argv0, const AppOptions &options) {
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

  // Rebuild the invocation, forcing the TUI (never the daemon/tray).
  vector<string> args = {term, "-e", exe, "--tui"};
  if (!options.packsDir.empty())
    args.insert(args.end(), {"--packs", options.packsDir});
  if (!options.devicePath.empty())
    args.insert(args.end(), {"--device", options.devicePath});

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
       << "Send SIGTERM to stop.\n";
#endif

  // Main loop: service the tray on a worker thread so we can refresh the title
  // and honour signals on this thread.
  thread trayThread;
#ifdef WAYCLACK_HAVE_DBUS
  if (trayReady)
    trayThread = thread([&] { tray.run(); });

  // Tray callbacks flip g_quit; stop() unblocks run() so the join returns.
  while (!g_quit.load()) {
    if (g_openTui.exchange(false))
      openTui(argv0, options);

    char title[256];
    snprintf(title, sizeof(title), "WayClack - %s%s %d%%",
             app.activePackName().c_str(), app.muted() ? " [muted]" : "",
             app.volume());
    if (title != lastTitle) {
      lastTitle = title;
      tray.setTitle(title);
    }
    usleep(200 * 1000);
  }

  tray.stop();
  if (trayThread.joinable())
    trayThread.join();
#else
  while (!g_quit.load())
    usleep(200 * 1000);
#endif

  app.stop();
  cout << "WayClack stopped.\n";
  return 0;
}

} // namespace WayClack
