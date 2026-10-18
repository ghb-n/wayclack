cmake_minimum_required(VERSION 3.14)
project(wayclack VERSION 1.0 LANGUAGES CXX)

# Set C++ standard to C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find the SDL2 and SDL2_mixer packages installed on your system
find_package(SDL2 REQUIRED)
find_package(SDL2_mixer REQUIRED)

# Create your executable target pointing to your source file(s)
add_executable(wayclack 
  src/mainwindow.cpp
  src/audio_driver/audio.cpp)

# Link SDL2 libraries to your wayclack executable
target_link_libraries(wayclack PRIVATE SDL2::SDL2 SDL2_mixer::SDL2_mixer)
