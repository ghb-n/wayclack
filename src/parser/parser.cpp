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
  // Legacy helper retained for backward compatibility; prefer getFileContent()
  // together with loadConfig()/loadUIConfig().
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
std::unordered_map<std::string, int>
WayClack::Parser::loadUIConfig(const std::string &jsonData) {
  auto j = nlohmann::json::parse(jsonData);
  std::unordered_map<std::string, int> configMap;

  if (j.contains("volume")) {
    // If volume is saved as a string like "100"
    if (j["volume"].is_string()) {
      try {
        configMap["volume"] = std::stoi(j["volume"].get<std::string>());
      } catch (const std::exception &e) {
        std::cerr << "Failed to convert volume string to int: " << e.what()
                  << "\n";
      }
    }
    // If volume is saved as a raw number like 100
    else if (j["volume"].is_number()) {
      configMap["volume"] = j["volume"].get<int>();
    }
  }
  return configMap;
}
