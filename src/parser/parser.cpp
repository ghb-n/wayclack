#include "parser.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

using namespace std;
using json = nlohmann::json;
using namespace WayClack;
std::string Parser::getFileContent(const std::string &filePath) {
  std::ifstream file(filePath, std::ios::in | std::ios::binary);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open file: " + filePath);
  }

  std::ostringstream buffer;
  buffer << file.rdbuf();

  return buffer.str();
}

void Parser::parse() {
  try {
    string rawData = getFileContent("config.json");

    json config = json::parse(rawData);

    string soundTheme = config["theme"];
    int volume = config["volume"];
    bool enabled = config["enabled"];
    json defines = config["defines"];
    std::unordered_map<std::string, std::string> keys = defines;
    cout << "Loaded theme: " << soundTheme << "\n";
    cout << "Volume level: " << volume << "\n";

    auto print = [](const auto &key, const auto &value) {
      cout << "Key:[" << key << "] Value:[" << value << "]\n";
    };

  } catch (const json::parse_error &e) {
    cerr << "JSON Parsing Error: " << e.byte << ":" << e.what() << "\n";
  } catch (const exception &e) {
    cerr << "JSON Parse Failed: " << e.what() << "\n";
  }
}

std::unordered_map<int, std::string>
WayClack::Parser::loadConfig(const std::string &jsonData) {
  unordered_map<int, std::string> keySounds;
  auto j = nlohmann::json::parse(jsonData);

  if (j.contains("defines")) {
    for (auto &[keyStr, soundPath] : j["defines"].items()) {
      int keyCode = stoi(keyStr);
      keySounds[keyCode] = soundPath;
    }
  }

  return keySounds;
}
