#include "mainwindow.h"
#include "audio_driver/audio.h"
#include "keyboard/keyboard.h"
#include "parser/parser.h"
#include <exception>
#include <iostream>
#include <nlohmann/json_fwd.hpp>
#include <unordered_map>

using namespace MW;
using namespace std;

void mw::gretting() { cout << "Hello, World!" << "\n"; }

int main() {
  WayClack::AudioPlayer player;
  WayClack::Keyboard keyboard;
  WayClack::Parser parser;
  mw m;

  m.gretting();

  if (!player.init()) {
    cerr << "Audio initalition failed! Exiting...\n";
    return 1;
  }

  std::string content;
  std::unordered_map<int, std::string> jsonContent;
  try {
    content = parser.getFileContent(
        "/home/thegrigorijagen/mechvibes_custom/Razer/config.json");
    cout << "Config Loaded Successfully!\n";
    jsonContent = parser.loadConfig(content);
    keyboard.listenToInput("/dev/input/event0", jsonContent, player);
  } catch (const exception &e) {
    cerr << "Error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
