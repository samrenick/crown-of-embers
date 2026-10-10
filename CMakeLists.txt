cmake_minimum_required(VERSION 3.16)
project(fire_emblem CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(RAYLIB REQUIRED IMPORTED_TARGET raylib)

add_executable(fe
  src/main.cpp
  src/game.cpp
  src/map.cpp
  src/combat.cpp
  src/level.cpp
)
target_compile_options(fe PRIVATE -Wall -Wextra)
target_link_libraries(fe PRIVATE PkgConfig::RAYLIB)

if(APPLE)
  target_link_libraries(fe PRIVATE "-framework Cocoa" "-framework IOKit" "-framework OpenGL" "-framework CoreVideo")
endif()
