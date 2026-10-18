#include "mainwindow.h"
#include "audio_driver/audio.h"
#include <iostream>

using namespace MW;
using namespace std;

void mw::gretting() {
  cout << "Hello, World!" << "\n";
}

int main() {
  WayClack::AudioPlayer player;
  mw m;
  
  m.gretting();
  
  if(player.init()) {
    player.loadSound("~/personal/soundpacks/test/click.wav");
    player.playSound();
    SDL_Delay(500);
  }

  return 0;
}
