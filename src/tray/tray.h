#pragma once

#include <atomic>
#include <functional>
#include <string>

namespace WayClack {

// Opaque implementation defined in tray.cpp (holds the D-Bus handles).
struct TrayIconImpl;

// Callbacks the tray invokes when the user picks a menu entry.
struct TrayCallbacks {
  std::function<void()> onMuteToggle;
  std::function<void()> onVolumeUp;
  std::function<void()> onVolumeDown;
  std::function<void()> onNextPack;
  std::function<void()> onPrevPack;
  std::function<void()> onOpenTui;
  std::function<void()> onQuit;
};

// A system-tray icon implemented directly on D-Bus using the
// StatusNotifierItem (SNI) + com.canonical.dbusmenu protocols.
//
// This is what Plasma (and most modern Linux panels) use for tray icons.
// Implementing it on raw libdbus keeps the project free of Qt/GTK/appindicator
// dependencies.
//
// Lifecycle: construct, call start(); run() blocks servicing the bus until
// stop() is called (e.g. from a menu action or a signal).
class TrayIcon {
public:
  TrayIcon();
  ~TrayIcon();

  TrayIcon(const TrayIcon &) = delete;
  TrayIcon &operator=(const TrayIcon &) = delete;

  // Connect to the session bus, register the item and menu object. Returns
  // false if there is no session bus or registration fails.
  bool start(const std::string &iconName, const TrayCallbacks &callbacks);

  // Block and dispatch D-Bus messages until stop() is called.
  void run();

  // Ask the event loop to exit and unregister the item.
  void stop();

  // Update the tray title/tooltip (e.g. active pack or volume).
  void setTitle(const std::string &title);

private:
  TrayIconImpl *impl_ = nullptr;

  std::atomic<bool> running_{false};
};

} // namespace WayClack
