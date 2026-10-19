#pragma once
#include <iostream>
#include <string>

namespace WayClack {
class Parser {
public:
  std::string getFileContent(const std::string &filePath);
  void parse();
};
} // namespace WayClack
