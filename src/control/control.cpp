#include "control.h"

#include <algorithm>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

using namespace std;

namespace WayClack {

namespace {

// Split a line into whitespace-separated tokens.
vector<string> tokenize(const string &line) {
  vector<string> out;
  istringstream ss(line);
  string tok;
  while (ss >> tok)
    out.push_back(tok);
  return out;
}

// Escape spaces so a token can carry free text (e.g. a status message).
string escapeField(const string &s) {
  string out;
  for (char c : s) {
    if (c == '\\')
      out += "\\\\";
    else if (c == ' ')
      out += "\\s";
    else if (c == '\n')
      out += "\\n";
    else
      out += c;
  }
  return out;
}

string unescapeField(const string &s) {
  string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      char n = s[++i];
      if (n == 's')
        out += ' ';
      else if (n == 'n')
        out += '\n';
      else
        out += n;
    } else {
      out += s[i];
    }
  }
  return out;
}

} // namespace

string controlSocketPath() {
  const char *runtime = getenv("XDG_RUNTIME_DIR");
  if (runtime != nullptr && *runtime != '\0')
    return string(runtime) + "/wayclack.sock";
  return "/tmp/wayclack-" + to_string(getuid()) + ".sock";
}

// ===========================================================================
// ControlServer
// ===========================================================================
ControlServer::~ControlServer() { stop(); }

bool ControlServer::start(App &app, std::atomic<bool> *quitFlag) {
  app_ = &app;
  quitFlag_ = quitFlag;
  path_ = controlSocketPath();

  // If a stale socket exists, probe it: a live daemon will accept, otherwise
  // we remove it and take over.
  struct sockaddr_un addr {};
  addr.sun_family = AF_UNIX;
  if (path_.size() >= sizeof(addr.sun_path))
    return false;
  strncpy(addr.sun_path, path_.c_str(), sizeof(addr.sun_path) - 1);

  int probe = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (probe >= 0) {
    bool live = connect(probe, reinterpret_cast<sockaddr *>(&addr),
                        sizeof(addr)) == 0;
    ::close(probe);
    if (live)
      return false; // Another daemon owns the socket.
    unlink(path_.c_str());
  }

  fd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd_ < 0)
    return false;

  // Non-blocking so accept() never stalls the main loop when there are no
  // pending connections.
  int flags = fcntl(fd_, F_GETFL, 0);
  fcntl(fd_, F_SETFL, flags | O_NONBLOCK);

  if (bind(fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    ::close(fd_);
    fd_ = -1;
    return false;
  }
  // Only the owner should talk to it.
  chmod(path_.c_str(), 0600);

  if (listen(fd_, 4) != 0) {
    ::close(fd_);
    fd_ = -1;
    unlink(path_.c_str());
    return false;
  }
  return true;
}

void ControlServer::stop() {
  for (auto &c : clients_)
    if (c.fd >= 0)
      ::close(c.fd);
  clients_.clear();
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  if (!path_.empty())
    unlink(path_.c_str());
}

void ControlServer::dropClient(Client &client) {
  if (client.fd >= 0)
    ::close(client.fd);
  client.fd = -1;
  client.in.clear();
}

void ControlServer::sendState(Client &client) {
  if (client.fd < 0 || app_ == nullptr)
    return;

  ostringstream os;
  os << "state" << " volume=" << app_->volume()
     << " muted=" << (app_->muted() ? 1 : 0)
     << " listening=" << (app_->listening() ? 1 : 0)
     << " selected=" << app_->selectedPack()
     << " device=" << escapeField(app_->device())
     << " devname=" << escapeField(app_->deviceName())
     << " status=" << escapeField(app_->status())
     << " packs=";
  const auto &packs = app_->packs();
  for (size_t i = 0; i < packs.size(); ++i) {
    if (i)
      os << ",";
    os << escapeField(packs[i].name);
  }
  os << "\n";

  string line = os.str();
  (void)write(client.fd, line.data(), line.size());
}

void ControlServer::broadcastEvent(const string &name, const string &sound,
                                   bool hadSound) {
  if (clients_.empty())
    return;
  string line = "key " + escapeField(name) + " " + escapeField(sound) + " " +
                (hadSound ? "1" : "0") + "\n";
  for (auto &c : clients_)
    if (c.fd >= 0)
      (void)write(c.fd, line.data(), line.size());
}

void ControlServer::pushKeyEvent(const string &name, const string &sound,
                                 bool hadSound) {
  broadcastEvent(name, sound, hadSound);
}

void ControlServer::handleLine(Client &client, const string &line) {
  if (app_ == nullptr)
    return;
  auto tok = tokenize(line);
  if (tok.empty())
    return;

  const string &cmd = tok[0];
  if (cmd == "state") {
    sendState(client);
  } else if (cmd == "volup") {
    app_->volumeUp();
    sendState(client);
  } else if (cmd == "voldown") {
    app_->volumeDown();
    sendState(client);
  } else if (cmd == "mute") {
    app_->toggleMute();
    sendState(client);
  } else if (cmd == "next") {
    app_->nextPack();
    sendState(client);
  } else if (cmd == "prev") {
    app_->prevPack();
    sendState(client);
  } else if (cmd == "select" && tok.size() >= 2) {
    app_->selectPack(stoi(tok[1]));
    sendState(client);
  } else if (cmd == "quit") {
    // The TUI asked the daemon to exit (tray Quit).
    app_->requestQuit();
    if (quitFlag_ != nullptr)
      *quitFlag_ = true;
    sendState(client);
  } else if (cmd == "ping") {
    string pong = "pong\n";
    (void)write(client.fd, pong.data(), pong.size());
  }
}

void ControlServer::poll() {
  if (fd_ < 0)
    return;

  // Accept new clients.
  for (;;) {
    int cfd = accept4(fd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (cfd < 0)
      break;
    Client c;
    c.fd = cfd;
    clients_.push_back(std::move(c));
    sendState(clients_.back());
  }

  // Read from existing clients.
  for (auto &c : clients_) {
    if (c.fd < 0)
      continue;
    char buf[512];
    for (;;) {
      ssize_t n = read(c.fd, buf, sizeof(buf));
      if (n > 0) {
        c.in.append(buf, static_cast<size_t>(n));
        // Process complete lines only.
        size_t pos;
        while ((pos = c.in.find('\n')) != string::npos) {
          string line = c.in.substr(0, pos);
          c.in.erase(0, pos + 1);
          if (!line.empty())
            handleLine(c, line);
        }
      } else if (n == 0) {
        dropClient(c); // Peer closed.
        break;
      } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
          break; // No more data right now.
        dropClient(c);
        break;
      }
    }
  }

  // Forget disconnected clients.
  clients_.erase(remove_if(clients_.begin(), clients_.end(),
                           [](const Client &c) { return c.fd < 0; }),
                 clients_.end());
}

// ===========================================================================
// ControlClient
// ===========================================================================
ControlClient::~ControlClient() { close(); }

bool ControlClient::connect() {
  string path = controlSocketPath();

  struct sockaddr_un addr {};
  addr.sun_family = AF_UNIX;
  if (path.size() >= sizeof(addr.sun_path))
    return false;
  strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0)
    return false;
  if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    ::close(fd);
    return false;
  }
  // Non-blocking so the TUI loop never stalls on the daemon.
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  fd_ = fd;

  sendLine("state"); // Ask for an initial snapshot.
  return true;
}

void ControlClient::close() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  buffer_.clear();
}

void ControlClient::sendLine(const string &line) {
  if (fd_ < 0)
    return;
  string msg = line + "\n";
  (void)write(fd_, msg.data(), msg.size());
}

void ControlClient::volumeUp() { sendLine("volup"); }
void ControlClient::volumeDown() { sendLine("voldown"); }
void ControlClient::toggleMute() { sendLine("mute"); }
void ControlClient::nextPack() { sendLine("next"); }
void ControlClient::prevPack() { sendLine("prev"); }
void ControlClient::selectPack(int index) {
  sendLine("select " + to_string(index));
}
void ControlClient::quitServer() { sendLine("quit"); }

void ControlClient::poll(vector<KeyEvent> *eventsOut) {
  if (fd_ < 0)
    return;

  char buf[1024];
  for (;;) {
    ssize_t n = read(fd_, buf, sizeof(buf));
    if (n > 0) {
      buffer_.append(buf, static_cast<size_t>(n));
    } else if (n == 0) {
      close();
      return;
    } else {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        break;
      close();
      return;
    }
  }

  size_t pos;
  while ((pos = buffer_.find('\n')) != string::npos) {
    string line = buffer_.substr(0, pos);
    buffer_.erase(0, pos + 1);
    if (line.empty())
      continue;

    auto tok = tokenize(line);
    if (tok.empty())
      continue;

    if (tok[0] == "state") {
      for (size_t i = 1; i < tok.size(); ++i) {
        size_t eq = tok[i].find('=');
        if (eq == string::npos)
          continue;
        string key = tok[i].substr(0, eq);
        string val = unescapeField(tok[i].substr(eq + 1));
        if (key == "volume") {
          state_.volume = stoi(val);
        } else if (key == "muted") {
          state_.muted = (val == "1");
        } else if (key == "listening") {
          state_.listening = (val == "1");
        } else if (key == "selected") {
          state_.selectedPack = stoi(val);
        } else if (key == "device") {
          state_.device = val;
        } else if (key == "devname") {
          state_.deviceName = val;
        } else if (key == "status") {
          state_.status = val;
        } else if (key == "packs") {
          state_.packs.clear();
          if (!val.empty()) {
            size_t start = 0, comma;
            while ((comma = val.find(',', start)) != string::npos) {
              state_.packs.push_back(val.substr(start, comma - start));
              start = comma + 1;
            }
            state_.packs.push_back(val.substr(start));
          }
        }
      }
    } else if (tok[0] == "key" && tok.size() >= 4 && eventsOut != nullptr) {
      KeyEvent e;
      e.name = unescapeField(tok[1]);
      e.sound = unescapeField(tok[2]);
      e.hadSound = (tok[3] == "1");
      eventsOut->push_back(e);
    }
  }
}

} // namespace WayClack
