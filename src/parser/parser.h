#pragma once
#include <iostream>
#include <string>
#include <unordered_map>

namespace WayClack {
class Parser {
public:
  std::string getFileContent(const std::string &filePath);
  std::unordered_map<int, std::string> loadConfig(const std::string &jsonData);
  void parse();
};
} // namespace WayClack
