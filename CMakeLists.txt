cmake_minimum_required(VERSION 3.14)
project(wayclack VERSION 1.0.0 LANGUAGES CXX)

# Expose the version to the source.
add_compile_definitions(WAYCLACK_VERSION="${PROJECT_VERSION}")

# Set C++ standard to C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# We use std::thread for the keyboard listener.
find_package(Threads REQUIRED)

# Find the SDL2 and SDL2_mixer packages installed on your system
find_package(SDL2 REQUIRED)
find_package(SDL2_mixer REQUIRED)
find_package(nlohmann_json 3.11.3 REQUIRED)

# Create your executable target pointing to your source file(s)
add_executable(wayclack
  src/mainwindow.cpp
  src/audio_driver/audio.cpp
  src/parser/parser.cpp
  src/keyboard/keyboard.cpp
  src/discovery/discovery.cpp
  src/cli/cli.cpp)

# Link SDL2 libraries to your wayclack executable
target_link_libraries(wayclack PRIVATE SDL2::SDL2
  SDL2_mixer::SDL2_mixer
  nlohmann_json::nlohmann_json
  Threads::Threads)
