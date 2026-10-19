#include "mainwindow.h"
#include "audio_driver/audio.h"
#include "keyboard/keyboard.h"
#include "parser/parser.h"
#include <exception>
#include <iostream>

using namespace MW;
using namespace std;

void mw::gretting() { cout << "Hello, World!" << "\n"; }

int main() {
  WayClack::AudioPlayer player;
  WayClack::Keyboard keyboard;
  WayClack::Parser parser;
  mw m;

  m.gretting();

  std::string content;
  try {
    content = parser.getFileContent(
        "/home/thegrigorijagen/mechvibes_custom/Razer/config.json");
    cout << "Config Loaded Successfully!\n";
  } catch (const exception &e) {
    cerr << "Error: " << e.what() << "\n";
    return 1;
  }

  try {
    player.loadSound(
        "/home/thegrigorijagen/personal/soundpacks/test/click.wav");
  } catch (const exception &e) {
    cerr << "Audio Error: " << e.what() << "\n";
    return 1;
  }

  try {
    keyboard.listenToInput("/dev/input/event3");
  } catch (const exception &e) {
    cerr << "Error: " << e.what() << "\n";
  }
  return 0;
}
