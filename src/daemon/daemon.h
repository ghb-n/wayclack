#pragma once

#include <string>

#include "../app/app.h"

namespace WayClack {

// Run WayClack with no terminal: the audio listener runs in the background and
// a system-tray icon (StatusNotifierItem) is shown in the panel. Returns a
// process exit code.
int runDaemon(const AppOptions &options);

} // namespace WayClack
