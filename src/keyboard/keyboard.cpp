#include "keyboard.h"
#include <cerrno>
#include <poll.h>

using namespace std;

namespace WayClack {

Keyboard::~Keyboard() { stop(); }

void Keyboard::setKeySounds(
    const std::unordered_map<int, std::string> &keySounds) {
  keySounds_ = keySounds;
}

bool Keyboard::start(const std::string &devicePath, AudioPlayer &player) {
  if (running_.load())
    return true;

  int fd = open(devicePath.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return false;

  devicePath_ = devicePath;
  player_ = &player;
  fd_.store(fd);
  running_.store(true);
  thread_ = std::thread(&Keyboard::readerLoop, this);
  return true;
}

void Keyboard::stop() {
  if (!running_.load())
    return;

  running_.store(false);

  // Close the descriptor to unblock the blocking read() promptly.
  int fd = fd_.exchange(-1);
  if (fd >= 0)
    close(fd);

  if (thread_.joinable())
    thread_.join();

  player_ = nullptr;
}

void Keyboard::readerLoop() {
  struct input_event ev;
  int fd = fd_.load();

  if (fd < 0)
    return;

  // Use poll() with a timeout so shutdown is responsive even though closing a
  // descriptor from another thread does not reliably interrupt a blocked
  // read() on Linux.
  struct pollfd pfd;
  pfd.fd = fd;
  pfd.events = POLLIN;

  while (running_.load()) {
    int ready = poll(&pfd, 1, 200);
    if (ready < 0) {
      if (errno == EINTR)
        continue;
      break;
    }
    if (ready == 0)
      continue; // Timeout: re-check running_.
    if (!(pfd.revents & POLLIN))
      break;

    ssize_t bytesRead = read(fd, &ev, sizeof(ev));
    if (bytesRead < (ssize_t)sizeof(ev)) {
      if (!running_.load())
        break;
      if (bytesRead < 0 && errno == EINTR)
        continue;
      break;
    }

    // Only report key-down (value == 1) for a non-repeat code.
    if (ev.type == EV_KEY && ev.value == 1) {
      KeyEvent event;
      event.code = ev.code;
      event.name = keyName(ev.code);

      auto it = keySounds_.find(ev.code);
      if (it != keySounds_.end()) {
        event.hadSound = true;
        event.sound = it->second;
        if (player_ != nullptr)
          player_->playSound(it->second);
      }

      {
        lock_guard<mutex> lock(eventsMutex_);
        events_.push_back(std::move(event));
        // Bound the buffer so a long idle session cannot grow unbounded.
        while (events_.size() > 64)
          events_.pop_front();
      }
    }
  }
}

std::deque<KeyEvent> Keyboard::drainEvents(size_t max) {
  lock_guard<mutex> lock(eventsMutex_);
  std::deque<KeyEvent> out;
  size_t take = std::min(max, events_.size());
  for (size_t i = 0; i < take; ++i) {
    out.push_back(std::move(events_.front()));
    events_.pop_front();
  }
  return out;
}

std::string Keyboard::keyName(int code) {
  // A compact lookup for the codes most packs map. Unknown codes fall back to
  // a numeric representation so the feed stays informative.
  static const unordered_map<int, string> names = {
      {KEY_ESC, "Esc"},         {KEY_1, "1"},           {KEY_2, "2"},
      {KEY_3, "3"},             {KEY_4, "4"},           {KEY_5, "5"},
      {KEY_6, "6"},             {KEY_7, "7"},           {KEY_8, "8"},
      {KEY_9, "9"},             {KEY_0, "0"},           {KEY_MINUS, "-"},
      {KEY_EQUAL, "="},         {KEY_BACKSPACE, "Backspace"},
      {KEY_TAB, "Tab"},         {KEY_Q, "Q"},           {KEY_W, "W"},
      {KEY_E, "E"},             {KEY_R, "R"},           {KEY_T, "T"},
      {KEY_Y, "Y"},             {KEY_U, "U"},           {KEY_I, "I"},
      {KEY_O, "O"},             {KEY_P, "P"},           {KEY_LEFTBRACE, "["},
      {KEY_RIGHTBRACE, "]"},    {KEY_ENTER, "Enter"},   {KEY_LEFTCTRL, "Ctrl"},
      {KEY_A, "A"},             {KEY_S, "S"},           {KEY_D, "D"},
      {KEY_F, "F"},             {KEY_G, "G"},           {KEY_H, "H"},
      {KEY_J, "J"},             {KEY_K, "K"},           {KEY_L, "L"},
      {KEY_SEMICOLON, ";"},     {KEY_APOSTROPHE, "'"},  {KEY_GRAVE, "`"},
      {KEY_LEFTSHIFT, "Shift"}, {KEY_BACKSLASH, "\\"},  {KEY_Z, "Z"},
      {KEY_X, "X"},             {KEY_C, "C"},           {KEY_V, "V"},
      {KEY_B, "B"},             {KEY_N, "N"},           {KEY_M, "M"},
      {KEY_COMMA, ","},         {KEY_DOT, "."},         {KEY_SLASH, "/"},
      {KEY_RIGHTSHIFT, "RShift"},
      {KEY_KPASTERISK, "KP*"},  {KEY_LEFTALT, "Alt"},   {KEY_SPACE, "Space"},
      {KEY_CAPSLOCK, "Caps"},   {KEY_F1, "F1"},         {KEY_F2, "F2"},
      {KEY_F3, "F3"},           {KEY_F4, "F4"},         {KEY_F5, "F5"},
      {KEY_F6, "F6"},           {KEY_F7, "F7"},         {KEY_F8, "F8"},
      {KEY_F9, "F9"},           {KEY_F10, "F10"},       {KEY_F11, "F11"},
      {KEY_F12, "F12"},         {KEY_UP, "Up"},         {KEY_DOWN, "Down"},
      {KEY_LEFT, "Left"},       {KEY_RIGHT, "Right"},   {KEY_HOME, "Home"},
      {KEY_END, "End"},         {KEY_PAGEUP, "PgUp"},   {KEY_PAGEDOWN, "PgDn"},
      {KEY_INSERT, "Ins"},      {KEY_DELETE, "Del"},    {KEY_RIGHTCTRL, "RCtrl"},
      {KEY_RIGHTALT, "RAlt"},
  };

  auto it = names.find(code);
  if (it != names.end())
    return it->second;
  return "KEY_" + std::to_string(code);
}

} // namespace WayClack
