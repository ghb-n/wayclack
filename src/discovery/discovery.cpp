#include "discovery.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <linux/input.h>
#include <nlohmann/json.hpp>
#include <sys/ioctl.h>
#include <unistd.h>

using namespace std;
using json = nlohmann::json;

namespace WayClack {
namespace Discovery {

std::string expandUser(const std::string &path) {
  if (path.empty() || path[0] != '~')
    return path;
  const char *home = getenv("HOME");
  if (home == nullptr)
    return path;
  if (path.size() == 1)
    return home;
  if (path[1] == '/')
    return string(home) + path.substr(1);
  return path;
}

static bool isDirectory(const string &path) {
  DIR *d = opendir(path.c_str());
  if (d == nullptr)
    return false;
  closedir(d);
  return true;
}

static bool fileExists(const string &path) {
  return access(path.c_str(), F_OK) == 0;
}

std::vector<PackInfo> findPacks(const string &baseDir) {
  vector<PackInfo> packs;
  string base = expandUser(baseDir);

  DIR *dir = opendir(base.c_str());
  if (dir == nullptr)
    return packs;

  struct dirent *entry;
  while ((entry = readdir(dir)) != nullptr) {
    string name = entry->d_name;
    if (name == "." || name == "..")
      continue;

    string dirPath = base + "/" + name;
    if (!isDirectory(dirPath))
      continue;

    string configPath = dirPath + "/config.json";
    if (!fileExists(configPath))
      continue;

    PackInfo info;
    info.name = name;
    info.dir = dirPath;
    info.config = configPath;

    // Read the display name/id from the config without failing the whole scan
    // if the file is malformed.
    try {
      ifstream f(configPath);
      if (f.is_open()) {
        json j;
        f >> j;
        if (j.contains("name") && j["name"].is_string())
          info.name = j["name"].get<string>();
        if (j.contains("id") && j["id"].is_string())
          info.id = j["id"].get<string>();
      }
    } catch (const exception &) {
      // Keep the directory-name fallback.
    }

    packs.push_back(std::move(info));
  }
  closedir(dir);

  // Sort by display name so ordering is stable across runs (readdir order is
  // filesystem-dependent).
  sort(packs.begin(), packs.end(),
       [](const PackInfo &a, const PackInfo &b) { return a.name < b.name; });

  return packs;
}

// ---------------------------------------------------------------------------
// Keyboard autodetection
// ---------------------------------------------------------------------------
namespace {

constexpr size_t kEvBitBytes = 96;   // Covers EV_MAX comfortably.
constexpr size_t kKeyBitBytes = 96;  // Covers KEY_MAX (0x2ff -> 768 bits).

// Read a device's sysfs name, or "" if unavailable.
string deviceName(const string &eventNode) {
  string base = eventNode.substr(eventNode.find_last_of('/') + 1);
  string path = "/sys/class/input/" + base + "/device/name";
  ifstream f(path);
  if (!f.is_open())
    return "";
  string name;
  getline(f, name);
  while (!name.empty() && (name.back() == '\n' || name.back() == '\r'))
    name.pop_back();
  return name;
}

// Find a by-id/by-path symlink that resolves to this event node. These are
// stable across reboots, so we prefer them as the returned path.
string stableSymlink(const string &eventNode) {
  const char *dirs[] = {"/dev/input/by-id", "/dev/input/by-path"};
  for (const char *dir : dirs) {
    DIR *d = opendir(dir);
    if (d == nullptr)
      continue;
    struct dirent *entry;
    while ((entry = readdir(d)) != nullptr) {
      string name = entry->d_name;
      if (name == "." || name == "..")
        continue;
      string link = string(dir) + "/" + name;
      char resolved[PATH_MAX];
      ssize_t n = readlink(link.c_str(), resolved, sizeof(resolved) - 1);
      if (n <= 0)
        continue;
      resolved[n] = '\0';
      // Symlinks point at "../eventN"; compare the basename.
      string target = resolved;
      string tail = target.substr(target.find_last_of('/') + 1);
      if (eventNode == "/dev/input/" + tail) {
        closedir(d);
        return link;
      }
    }
    closedir(d);
  }
  return "";
}

// Read and score a single event node. Returns false if it is not a plausible
// keyboard at all, otherwise fills `info` (score may still be low).
bool scoreDevice(const string &eventNode, DeviceInfo &info) {
  int fd = open(eventNode.c_str(), O_RDONLY | O_NONBLOCK);
  if (fd < 0)
    return false;

  unsigned long evbits[kEvBitBytes / sizeof(unsigned long)] = {0};
  unsigned long keybits[kKeyBitBytes / sizeof(unsigned long)] = {0};

  bool haveEv = ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) >= 0;
  ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits);
  close(fd);

  if (!haveEv)
    return false;

  auto hasEv = [&](int ev) {
    return (evbits[ev / (sizeof(unsigned long) * 8)] &
            (1UL << (ev % (sizeof(unsigned long) * 8)))) != 0;
  };
  auto hasKey = [&](int code) {
    return (keybits[code / (sizeof(unsigned long) * 8)] &
            (1UL << (code % (sizeof(unsigned long) * 8)))) != 0;
  };

  if (!hasEv(EV_KEY))
    return false;

  // Core typing keys: a real keyboard must be able to type a sentence.
  const int required[] = {KEY_A, KEY_Z, KEY_ENTER, KEY_SPACE, KEY_LEFTSHIFT};
  for (int code : required) {
    if (!hasKey(code))
      return false;
  }

  info.path = eventNode;
  info.name = deviceName(eventNode);
  info.stablePath = stableSymlink(eventNode);

  int score = 0;

  // Reward alphabetic/digit coverage. A full 104-key board lights up most of
  // these; a stripped-down key interface lights up far fewer.
  static const int typedKeys[] = {
      KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
      KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
      KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z, KEY_1, KEY_2, KEY_3, KEY_4,
      KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0, KEY_SEMICOLON, KEY_APOSTROPHE,
      KEY_COMMA, KEY_DOT, KEY_SLASH, KEY_MINUS, KEY_EQUAL, KEY_LEFTBRACE,
      KEY_RIGHTBRACE, KEY_BACKSLASH, KEY_GRAVE, KEY_TAB, KEY_BACKSPACE};
  for (int code : typedKeys) {
    if (hasKey(code))
      score += 2;
  }

  // Function keys and navigation add confidence.
  static const int extraKeys[] = {
      KEY_F1,  KEY_F2,  KEY_F3,  KEY_F4,  KEY_F5,  KEY_F6, KEY_F7,
      KEY_F8,  KEY_F9,  KEY_F10, KEY_F11, KEY_F12, KEY_ESC,
      KEY_UP,  KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_HOME, KEY_END};
  for (int code : extraKeys) {
    if (hasKey(code))
      score += 1;
  }

  // --- Signals for a built-in laptop keyboard ---
  // i8042/serio is the classic AT/PS2 controller used by internal keyboards.
  string realPath;
  {
    char buf[PATH_MAX];
    string base = eventNode.substr(eventNode.find_last_of('/') + 1);
    string sysPath = "/sys/class/input/" + base;
    ssize_t n = readlink(sysPath.c_str(), buf, sizeof(buf) - 1);
    if (n > 0) {
      buf[n] = '\0';
      realPath = buf;
    }
  }
  if (realPath.find("i8042") != string::npos ||
      realPath.find("serio") != string::npos)
    score += 30;

  string lower = info.name;
  for (char &c : lower)
    c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

  if (lower.find("keyboard") != string::npos)
    score += 15;
  if (lower.find("at translated set") != string::npos)
    score += 15; // Classic built-in PS/2 keyboard name.

  // A by-id/by-path name tagged as a keyboard interface is a good sign.
  if (info.stablePath.find("-event-kbd") != string::npos)
    score += 10;

  // --- Penalise pointer-like devices ---
  // A mouse (or a combo device whose sibling interface is a mouse) often
  // exposes a HID "Keyboard" interface; down-rank it so the true keyboard wins.
  bool hasPointerAxes = hasEv(EV_REL) || hasEv(EV_ABS);
  bool hasMouseButtons = hasKey(BTN_LEFT) || hasKey(BTN_RIGHT) ||
                         hasKey(BTN_MIDDLE) || hasKey(BTN_MOUSE);
  if (hasMouseButtons)
    score -= 60;
  if (hasPointerAxes)
    score -= 40;
  if (lower.find("mouse") != string::npos)
    score -= 50;
  if (lower.find("touchpad") != string::npos)
    score -= 50;
  if (info.stablePath.find("-event-mouse") != string::npos)
    score -= 50;

  // --- Penalise button/media-only nodes ---
  // Power/sleep/lid/video-bus style devices lack the full typing set but do
  // expose EV_KEY, so treat a thin key set as unlikely.
  int keyCount = 0;
  for (size_t i = 0; i < sizeof(keybits) / sizeof(keybits[0]); ++i)
    keyCount += __builtin_popcountl(keybits[i]);
  if (keyCount < 40)
    score -= 25;

  const char *noiseNames[] = {"power button", "sleep button", "lid switch",
                              "video bus",     "pc speaker",     "hda ",
                              "headphone",     "hdmi",           "hid events",
                              "consumer control"};
  for (const char *noise : noiseNames) {
    if (lower.find(noise) != string::npos) {
      score -= 40;
      break;
    }
  }

  info.score = score;
  return true;
}

} // namespace

std::vector<DeviceInfo> listKeyboardCandidates() {
  vector<DeviceInfo> candidates;

  DIR *dir = opendir("/dev/input");
  if (dir == nullptr)
    return candidates;

  struct dirent *entry;
  while ((entry = readdir(dir)) != nullptr) {
    string name = entry->d_name;
    if (name.rfind("event", 0) != 0)
      continue;
    // Skip non-numeric suffixes (defensive).
    bool numeric = name.size() > 5;
    for (size_t i = 5; i < name.size() && numeric; ++i)
      if (!isdigit(static_cast<unsigned char>(name[i])))
        numeric = false;
    if (!numeric)
      continue;

    DeviceInfo info;
    if (scoreDevice("/dev/input/" + name, info))
      candidates.push_back(std::move(info));
  }
  closedir(dir);

  // Highest score wins; ties break toward the lower event index for
  // determinism.
  sort(candidates.begin(), candidates.end(),
       [](const DeviceInfo &a, const DeviceInfo &b) {
         if (a.score != b.score)
           return a.score > b.score;
         return a.path < b.path;
       });

  return candidates;
}

std::string findKeyboardDevice(DeviceInfo *infoOut) {
  vector<DeviceInfo> candidates = listKeyboardCandidates();
  if (candidates.empty())
    return "";

  const DeviceInfo &best = candidates.front();
  if (best.score <= 0)
    return "";

  if (infoOut != nullptr)
    *infoOut = best;

  // Prefer the stable by-id/by-path symlink when available.
  return best.stablePath.empty() ? best.path : best.stablePath;
}

std::string findKeyboardDevice() { return findKeyboardDevice(nullptr); }

} // namespace Discovery
} // namespace WayClack
