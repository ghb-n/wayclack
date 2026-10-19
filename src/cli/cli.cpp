#include "cli.h"
#include "../parser/parser.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>

using namespace std;

namespace WayClack {

// ---------------------------------------------------------------------------
// ANSI helper
// ---------------------------------------------------------------------------
namespace {

// Strip ANSI escapes so we can measure visible width for alignment.
const char *kReset = "\033[0m";
const char *kBold = "\033[1m";
const char *kDim = "\033[2m";
const char *kCyan = "\033[36m";
const char *kGreen = "\033[32m";
const char *kYellow = "\033[33m";
const char *kRed = "\033[31m";
const char *kHideCursor = "\033[?25l";
const char *kShowCursor = "\033[?25h";

size_t visibleLength(const string &s) {
  size_t len = 0;
  bool inEscape = false;
  for (size_t i = 0; i < s.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (c == '\033') {
      inEscape = true;
      continue;
    }
    if (inEscape) {
      if (c == 'm')
        inEscape = false;
      continue;
    }
    // Count the lead byte of a UTF-8 sequence only; continuation bytes
    // (10xxxxxx) do not start a glyph.
    if ((c & 0xC0) != 0x80)
      ++len;
  }
  return len;
}

// Truncate to `width` visible columns, preserving ANSI escapes and appending a
// reset so color state never bleeds into the border.
string truncateTo(const string &s, size_t width) {
  if (visibleLength(s) <= width)
    return s;

  string out;
  size_t visible = 0;
  bool inEscape = false;
  bool truncated = false;
  size_t reserve = (width > 0) ? 1 : 0; // Room for the ellipsis glyph.

  for (size_t i = 0; i < s.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (c == '\033') {
      inEscape = true;
      out += c;
      continue;
    }
    if (inEscape) {
      out += c;
      if (c == 'm')
        inEscape = false;
      continue;
    }
    if ((c & 0xC0) == 0x80) {
      // Continuation byte: part of the previous glyph.
      out += c;
      continue;
    }
    if (visible >= width - reserve) {
      truncated = true;
      break;
    }
    out += c;
    ++visible;
  }

  if (truncated) {
    if (visible >= width && width > 0) {
      // Drop one char to make room for the ellipsis.
      out = out.substr(0, out.size() - 1);
    }
    out += "\u2026";
  }
  out += kReset;
  return out;
}

string padTo(const string &s, size_t width) {
  size_t len = visibleLength(s);
  if (len >= width)
    return s;
  return s + string(width - len, ' ');
}
} // namespace

// ---------------------------------------------------------------------------
// Terminal lifecycle
// ---------------------------------------------------------------------------

CLI::~CLI() { leaveRawMode(); }

bool CLI::enterRawMode() {
  if (!isatty(STDIN_FILENO))
    return false;

  termFd_ = STDIN_FILENO;
  if (tcgetattr(termFd_, &savedTermios_) != 0)
    return false;

  termios raw = savedTermios_;
  // Character-at-a-time, no echo, no signal generation (we want Ctrl+C as a
  // key so the UI can shut down cleanly).
  raw.c_lflag &= ~(ICANON | ECHO | ISIG);
  raw.c_iflag &= ~(IXON | ICRNL);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;

  if (tcsetattr(termFd_, TCSAFLUSH, &raw) != 0)
    return false;

  rawMode_ = true;
  return true;
}

void CLI::leaveRawMode() {
  if (rawMode_) {
    tcsetattr(termFd_, TCSAFLUSH, &savedTermios_);
    rawMode_ = false;
  }
}

void CLI::moveCursorToBottom() {
  // Park the cursor on the last row so the terminal does not scroll.
  printf("\033[%d;%dH", termHeight_, 1);
  fflush(stdout);
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void CLI::drawVolumeBar(string &out, int width) const {
  int volume = attached_ ? (client_ ? client_->state().volume : 0)
                         : (player_ ? player_->volume() : 0);
  bool muted = attached_ ? (client_ ? client_->state().muted : false)
                         : (player_ ? player_->muted() : false);

  if (width < 10)
    width = 10;

  const char *fillColor = kGreen;
  if (volume >= 80)
    fillColor = kRed;
  else if (volume >= 50)
    fillColor = kYellow;
  if (muted)
    fillColor = kDim;

  int filled = (volume * width) / 100;

  out += "  ";
  out += muted ? "[MUTED] " : "[VOL]   ";
  out += fillColor;
  for (int i = 0; i < width; ++i)
    out += (i < filled) ? "\u2588" : "\u2591";
  out += kReset;
  out += " ";

  ostringstream vol;
  vol.width(3);
  vol << volume << "%";
  out += vol.str();
}

string CLI::buildFrame() const {
  // Refresh terminal dimensions each frame.
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
    const_cast<CLI *>(this)->termWidth_ = ws.ws_col;
    const_cast<CLI *>(this)->termHeight_ = ws.ws_row;
  }

  int W = max(40, termWidth_ - 2);
  string out;
  out += kHideCursor;

  // Pull the values we display from whichever mode is active.
  const bool listening = attached_ ? (client_ && client_->state().listening)
                                   : listenerOk_;
  const string devNode = attached_ ? (client_ ? client_->state().device : "")
                                   : device_;
  const string devName = attached_
                             ? (client_ ? client_->state().deviceName : "")
                             : deviceName_;
  const size_t packCount = attached_
                               ? (client_ ? client_->state().packs.size() : 0)
                               : packs_.size();
  const int selected = attached_
                           ? (client_ ? client_->state().selectedPack : -1)
                           : selectedPack_;
  const bool muted = attached_ ? (client_ && client_->state().muted)
                               : (player_ && player_->muted());
  const string status = attached_
                            ? (client_ ? client_->state().status : "")
                            : statusMessage_;
  const vector<string> packNames = [&] {
    vector<string> names;
    if (attached_) {
      if (client_)
        names = client_->state().packs;
    } else {
      for (const auto &p : packs_)
        names.push_back(p.name);
    }
    return names;
  }();

  auto hr = [&](char l, char m, char r) {
    string line;
    line += l;
    for (int i = 0; i < W - 2; ++i)
      line += m;
    line += r;
    return line;
  };
  auto row = [&](const string &content) {
    ostringstream s;
    s << "\u2502" << padTo(truncateTo(content, W - 2), W - 2) << "\u2502";
    return s.str();
  };

  // --- Title ---
  out += hr('+', '=', '+') + "\n";
  string title = string(kBold) + kCyan + " WAYCLACK " + kReset +
                 " mechanical keyboard audio ";
  if (attached_)
    title += string(kDim) + " (attached) ";
  string state = listening ? (string(kGreen) + "LISTENING" + kReset)
                           : (string(kRed) + "STOPPED" + kReset);
  string titleLine = title + "  " + state;
  out += row(titleLine) + "\n";
  out += hr('+', '=', '+') + "\n";

  // --- Two-column body: status | packs ---
  // Give the status column a comfortable minimum so device names fit, while
  // leaving the pack list enough room.
  int colLeft = max(34, W / 3);
  if (colLeft > W - 20)
    colLeft = W - 20; // Never squeeze the pack list out entirely.
  int colRight = W - colLeft - 3;

  vector<string> left, right;

  // Show the human-readable device name (e.g. "AT Translated Set 2 keyboard")
  // rather than the opaque event node.
  string deviceLabel = devNode.empty() ? "<none>"
                        : devName.empty() ? devNode
                                          : devName;

  left.push_back(string(kBold) + "Status" + kReset);
  left.push_back(string(kDim) + "dev" + kReset + "   " + deviceLabel);
  left.push_back(string(kDim) + "node" + kReset + "  " +
                 (devNode.empty() ? "<none>" : devNode));
  left.push_back(string(kDim) + "packs" + kReset + "   " +
                 to_string(packCount));
  left.push_back(string(kDim) + "active" + kReset + "  " +
                 (selected >= 0 ? "yes" : "no"));
  left.push_back(string(kDim) + "sound" + kReset + "   " +
                 (muted ? "muted" : "on"));
  if (!status.empty()) {
    left.push_back("");
    left.push_back(string(kYellow) + status + kReset);
  }

  right.push_back(string(kBold) + "Sound Packs" + kReset);
  if (packNames.empty()) {
    right.push_back(string(kDim) + "(no packs found)" + kReset);
  } else {
    for (size_t i = 0; i < packNames.size(); ++i) {
      bool isSelected = (static_cast<int>(i) == selected);
      string marker = isSelected ? string(kGreen) + ">" + kReset : " ";
      string label = truncateTo(packNames[i], colRight - 4);
      string line = marker + " " + label;
      if (isSelected)
        line = string(kBold) + line + kReset;
      right.push_back(line);
    }
  }

  size_t rows = max(left.size(), right.size());
  for (size_t i = 0; i < rows; ++i) {
    string l = i < left.size() ? left[i] : "";
    string r = i < right.size() ? right[i] : "";
    l = padTo(truncateTo(l, colLeft), colLeft);
    r = padTo(truncateTo(r, colRight), colRight);
    out += "\u2502" + l + "\u2502 " + r + "\u2502\n";
  }
  out += hr('+', '-', '+') + "\n";

  // --- Volume bar ---
  string volLine = "  Volume";
  string bar;
  drawVolumeBar(bar, max(10, W - 30));
  out += row(volLine + bar) + "\n";
  out += hr('+', '-', '+') + "\n";

  // --- Keystroke feed ---
  out += row(string(kBold) + "Keystroke Feed" + kReset) + "\n";
  const int feedRows = 5;
  for (int i = 0; i < feedRows; ++i) {
    string line;
    // Show most recent first.
    if (i < static_cast<int>(feed_.size())) {
      line = feed_[feed_.size() - 1 - i];
    }
    out += row("  " + truncateTo(line, W - 6)) + "\n";
  }
  out += hr('+', '-', '+') + "\n";

  // --- Controls ---
  string controls =
      attached_
          ? string(kDim) +
                "  [+/-] volume   [m] mute   [n/p] next/prev pack   "
                "[r] reload   [x] stop daemon   [q] detach" +
                kReset
          : string(kDim) +
                "  [+/-] volume   [m] mute   [n/p] next/prev pack   "
                "[r] reload   [q] quit" +
                kReset;
  out += row(controls) + "\n";
  out += hr('+', '=', '+') + "\n";

  return out;
}

void CLI::render() {
  string frame = buildFrame();
  // Home + clear-in-place avoids the flicker of a full clear each frame.
  fputs("\033[H", stdout);
  fputs(frame.c_str(), stdout);
  fputs("\033[J", stdout);
  fflush(stdout);
}

// ---------------------------------------------------------------------------
// Pack loading
// ---------------------------------------------------------------------------

vector<string> CLI::collectPackSounds(const string &configPath,
                                      int &outVolume) {
  vector<string> sounds;
  try {
    Parser parser;
    string content = parser.getFileContent(configPath);

    // Keys map to sample filenames.
    auto keySounds = parser.loadConfig(content);
    for (const auto &[code, sound] : keySounds)
      sounds.push_back(sound);

    // The config may also reference a default "sound".
    auto j = nlohmann::json::parse(content);
    if (j.contains("sound") && j["sound"].is_string())
      sounds.push_back(j["sound"].get<string>());

    outVolume = 100;
    auto ui = parser.loadUIConfig(content);
    auto it = ui.find("volume");
    if (it != ui.end())
      outVolume = it->second;
  } catch (const exception &e) {
    statusMessage_ = string("config error: ") + e.what();
  }

  // De-duplicate.
  sort(sounds.begin(), sounds.end());
  sounds.erase(unique(sounds.begin(), sounds.end()), sounds.end());
  return sounds;
}

void CLI::loadPack(size_t index, bool resetVolume) {
  if (index >= packs_.size() || player_ == nullptr || keyboard_ == nullptr)
    return;

  const PackInfo &pack = packs_[index];

  int volume = 100;
  vector<string> sounds = collectPackSounds(pack.config, volume);

  keyboard_->setKeySounds({});
  player_->clearCache();
  player_->preload(pack.dir, sounds);

  // Rebuild the key->sound map (paths are relative to the pack dir).
  try {
    Parser parser;
    string content = parser.getFileContent(pack.config);
    keyboard_->setKeySounds(parser.loadConfig(content));
  } catch (const exception &) {
    // Leaves the map empty; the status message already reflects the failure.
  }

  if (resetVolume)
    player_->setVolume(volume);

  selectedPack_ = static_cast<int>(index);
  statusMessage_ = "loaded: " + pack.name;
}

// ---------------------------------------------------------------------------
// Input handling
// ---------------------------------------------------------------------------

bool CLI::handleKey(int key) {
  switch (key) {
  case 'q':
  case 'Q':
  case 3: // Ctrl+C
    running_ = false;
    return false;
  case '+':
  case '=':
    player_->setVolume(player_->volume() + 5);
    player_->setMuted(false);
    break;
  case '-':
  case '_':
    player_->setVolume(player_->volume() - 5);
    break;
  case 'm':
  case 'M':
    player_->setMuted(!player_->muted());
    statusMessage_ = player_->muted() ? "muted" : "unmuted";
    break;
  case 'n':
  case 'N':
    if (!packs_.empty())
      loadPack((selectedPack_ + 1) % packs_.size(), true);
    break;
  case 'p':
  case 'P':
    if (!packs_.empty())
      loadPack((selectedPack_ - 1 + packs_.size()) % packs_.size(), true);
    break;
  case 'r':
  case 'R':
    if (selectedPack_ >= 0)
      loadPack(selectedPack_, true);
    break;
  default:
    break;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Attached mode (drives a running daemon over the control socket)
// ---------------------------------------------------------------------------

bool CLI::handleAttachedKey(int key) {
  if (client_ == nullptr)
    return false;

  switch (key) {
  case 'q':
  case 'Q':
  case 3: // Ctrl+C — detach only; the daemon keeps running.
    running_ = false;
    return false;
  case '+':
  case '=':
    client_->volumeUp();
    break;
  case '-':
  case '_':
    client_->volumeDown();
    break;
  case 'm':
  case 'M':
    client_->toggleMute();
    break;
  case 'n':
  case 'N':
    client_->nextPack();
    break;
  case 'p':
  case 'P':
    client_->prevPack();
    break;
  case 'r':
  case 'R':
    // Reload == re-select the current pack.
    client_->selectPack(client_->state().selectedPack);
    break;
  case 'x':
  case 'X':
    // Quit the daemon itself (stops the tray too).
    client_->quitServer();
    break;
  default:
    break;
  }
  return true;
}

void CLI::refreshAttachedState() {
  if (client_ == nullptr)
    return;
  vector<ControlClient::KeyEvent> events;
  client_->poll(&events);
  for (auto &e : events) {
    string line = string(kCyan) + "  " + padTo(e.name, 10) + kReset;
    if (e.hadSound)
      line += string(kGreen) + "\u266a " + e.sound + kReset;
    else
      line += string(kDim) + "(no sample)" + kReset;
    feed_.push_back(line);
    while (feed_.size() > 32)
      feed_.pop_front();
  }
}

int CLI::runAttached() {
  client_ = new ControlClient();
  attached_ = true;

  if (!client_->connect()) {
    delete client_;
    client_ = nullptr;
    cerr << "No running WayClack daemon found.\n"
         << "Start one with:  wayclack --daemon\n";
    return 1;
  }

  if (!enterRawMode()) {
    cerr << "This UI needs an interactive terminal (TTY).\n";
    return 1;
  }

  bool lostConnection = false;
  while (running_) {
    refreshAttachedState();
    if (!client_->connected()) {
      // The daemon went away (tray Quit, crash, or logout).
      lostConnection = true;
      break;
    }
    render();

    unsigned char buf[16];
    ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
    if (n > 0) {
      for (ssize_t i = 0; i < n; ++i) {
        if (buf[i] == '\033')
          continue; // Skip escape sequences.
        if (!handleAttachedKey(buf[i]))
          break;
      }
    }
    this_thread::sleep_for(chrono::milliseconds(50));
  }

  leaveRawMode();
  fputs("\033[2J\033[H", stdout);
  fputs(kShowCursor, stdout);
  if (lostConnection)
    cout << "WayClack stopped; detached.\n";
  else
    cout << "Detached from WayClack (the tray keeps running).\n";

  delete client_;
  client_ = nullptr;
  attached_ = false;
  return 0;
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------

int CLI::run(const TuiOptions &options) {
  player_ = new AudioPlayer();
  keyboard_ = new Keyboard();

  if (!player_->init()) {
    cerr << "Audio initialization failed! Exiting...\n";
    return 1;
  }

  // Discover packs.
  packs_ = Discovery::findPacks(options.packsDir);
  if (packs_.empty()) {
    cerr << "No sound packs found in: "
         << Discovery::expandUser(options.packsDir) << "\n";
    return 1;
  }

  // Discover device.
  device_ = options.devicePath;
  if (device_.empty()) {
    WayClack::DeviceInfo info;
    device_ = Discovery::findKeyboardDevice(&info);
    deviceName_ = info.name;
  }

  // Load the first pack.
  loadPack(0, true);
  if (options.startMuted)
    player_->setMuted(true);

  // Start the listener on its own thread.
  if (device_.empty()) {
    statusMessage_ = "no keyboard device found (try --device)";
    listenerOk_ = false;
  } else {
    listenerOk_ = keyboard_->start(device_, *player_);
    if (!listenerOk_)
      statusMessage_ = "cannot open " + device_ + " (input group? root?)";
  }

  if (!enterRawMode()) {
    cerr << "This UI needs an interactive terminal (TTY).\n";
    return 1;
  }

  // Run until quit.
  auto lastRender = chrono::steady_clock::now();
  while (running_) {
    // Drain keypresses from the listener into the feed.
    auto events = keyboard_->drainEvents(64);
    for (auto &e : events) {
      string line = string(kCyan) + "  " + padTo(e.name, 10) + kReset;
      if (e.hadSound)
        line += string(kGreen) + "\u266a " + e.sound + kReset;
      else
        line += string(kDim) + "(no sample)" + kReset;
      feed_.push_back(line);
      while (feed_.size() > 32)
        feed_.pop_front();
    }

    render();

    // Read available input, non-blocking (VMIN=0).
    unsigned char buf[16];
    ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
    if (n > 0) {
      for (ssize_t i = 0; i < n; ++i) {
        if (buf[i] == '\033') {
          // Skip an escape sequence (arrow keys etc.).
          continue;
        }
        if (!handleKey(buf[i]))
          break;
      }
    }

    // ~20 FPS is plenty and keeps CPU usage low.
    this_thread::sleep_for(chrono::milliseconds(50));
  }

  leaveRawMode();
  keyboard_->stop();

  // Clear the screen and restore the prompt.
  fputs("\033[2J\033[H", stdout);
  fputs(kShowCursor, stdout);
  cout << "WayClack stopped.\n";

  delete keyboard_;
  delete player_;
  keyboard_ = nullptr;
  player_ = nullptr;
  return 0;
}

} // namespace WayClack
