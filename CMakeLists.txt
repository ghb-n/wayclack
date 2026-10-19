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

# The system-tray icon is implemented directly on libdbus (no Qt/GTK/
# appindicator). It is optional: if the dbus headers are missing the TUI still
# builds, just without a tray icon.
find_package(PkgConfig QUIET)
set(WAYCLACK_HAVE_DBUS OFF)
if(PkgConfig_FOUND)
  pkg_check_modules(DBUS dbus-1)
  if(DBUS_FOUND)
    set(WAYCLACK_HAVE_DBUS ON)
  endif()
endif()

# Create your executable target pointing to your source file(s)
add_executable(wayclack
  src/mainwindow.cpp
  src/app/app.cpp
  src/daemon/daemon.cpp
  src/audio_driver/audio.cpp
  src/parser/parser.cpp
  src/keyboard/keyboard.cpp
  src/discovery/discovery.cpp
  src/cli/cli.cpp)

if(WAYCLACK_HAVE_DBUS)
  target_sources(wayclack PRIVATE src/tray/tray.cpp)
  target_compile_definitions(wayclack PRIVATE WAYCLACK_HAVE_DBUS=1)
  target_include_directories(wayclack PRIVATE ${DBUS_INCLUDE_DIRS})
  target_link_directories(wayclack PRIVATE ${DBUS_LIBRARY_DIRS})
  message(STATUS "WayClack: D-Bus tray icon enabled (${DBUS_VERSION})")
else()
  message(STATUS "WayClack: D-Bus not found - tray icon disabled")
endif()

# Link SDL2 libraries to your wayclack executable
target_link_libraries(wayclack PRIVATE SDL2::SDL2
  SDL2_mixer::SDL2_mixer
  nlohmann_json::nlohmann_json
  Threads::Threads)

if(WAYCLACK_HAVE_DBUS)
  target_link_libraries(wayclack PRIVATE ${DBUS_LIBRARIES})
endif()
