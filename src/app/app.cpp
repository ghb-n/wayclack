#include "app.h"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <sstream>

#include "../parser/parser.h"

using namespace std;

namespace WayClack {

App::~App() { stop(); }

namespace {

// Gather every sample filename a pack references, plus its configured volume.
vector<string> collectPackSounds(const string &configPath, int &outVolume) {
  vector<string> sounds;
  try {
    Parser parser;
    string content = parser.getFileContent(configPath);

    auto keySounds = parser.loadConfig(content);
    for (const auto &[code, sound] : keySounds)
      sounds.push_back(sound);

    auto j = nlohmann::json::parse(content);
    if (j.contains("sound") && j["sound"].is_string())
      sounds.push_back(j["sound"].get<string>());

    outVolume = 100;
    auto ui = parser.loadUIConfig(content);
    auto it = ui.find("volume");
    if (it != ui.end())
      outVolume = it->second;
  } catch (const exception &) {
    // The caller surfaces "no samples" via the empty list.
  }

  sort(sounds.begin(), sounds.end());
  sounds.erase(unique(sounds.begin(), sounds.end()), sounds.end());
  return sounds;
}

} // namespace

bool App::start(const AppOptions &options) {
  if (!player_.init()) {
    reason_ = "audio initialization failed";
    return false;
  }

  packs_ = Discovery::findPacks(options.packsDir);
  if (packs_.empty()) {
    reason_ = "no sound packs found in " +
              Discovery::expandUser(options.packsDir);
    return false;
  }

  device_ = options.devicePath;
  if (device_.empty()) {
    DeviceInfo info;
    device_ = Discovery::findKeyboardDevice(&info);
    deviceName_ = info.name;
  }

  loadPack(0, true);
  if (options.startMuted)
    player_.setMuted(true);

  if (device_.empty()) {
    status_ = "no keyboard device found (try --device)";
  } else if (!keyboard_.start(device_, player_)) {
    status_ = "cannot open " + device_ + " (input group? root?)";
  } else {
    status_ = "listening on " + device_;
  }
  return true;
}

void App::stop() { keyboard_.stop(); }

void App::loadPack(size_t index, bool resetVolume) {
  if (index >= packs_.size())
    return;

  const PackInfo &pack = packs_[index];

  int volume = 100;
  vector<string> sounds = collectPackSounds(pack.config, volume);

  keyboard_.setKeySounds({});
  player_.clearCache();
  player_.preload(pack.dir, sounds);

  try {
    Parser parser;
    string content = parser.getFileContent(pack.config);
    keyboard_.setKeySounds(parser.loadConfig(content));
  } catch (const exception &) {
    // Leaves the map empty; sampling still works via the default sample.
  }

  if (resetVolume)
    player_.setVolume(volume);

  selectedPack_ = static_cast<int>(index);
  status_ = "loaded: " + pack.name;
}

void App::volumeUp(int step) {
  lock_guard<mutex> lock(mutex_);
  player_.setVolume(player_.volume() + step);
  player_.setMuted(false);
}

void App::volumeDown(int step) {
  lock_guard<mutex> lock(mutex_);
  player_.setVolume(player_.volume() - step);
}

void App::toggleMute() {
  lock_guard<mutex> lock(mutex_);
  player_.setMuted(!player_.muted());
  status_ = player_.muted() ? "muted" : "unmuted";
}

void App::nextPack() {
  lock_guard<mutex> lock(mutex_);
  if (!packs_.empty())
    loadPack((selectedPack_ + 1) % packs_.size(), true);
}

void App::prevPack() {
  lock_guard<mutex> lock(mutex_);
  if (!packs_.empty())
    loadPack((selectedPack_ - 1 + packs_.size()) % packs_.size(), true);
}

void App::selectPack(int index) {
  lock_guard<mutex> lock(mutex_);
  if (index < 0 || index >= static_cast<int>(packs_.size()))
    return;
  loadPack(static_cast<size_t>(index), true);
}

string App::activePackName() const {
  if (selectedPack_ < 0 || selectedPack_ >= static_cast<int>(packs_.size()))
    return "<none>";
  return packs_[selectedPack_].name;
}

} // namespace WayClack
