cmake_minimum_required(VERSION 3.16)
project(cshvh LANGUAGES C CXX)

# Counter-Strike: HvH
#   cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release -j
# raylib 6.2 is fetched automatically (or set -DFETCHCONTENT_SOURCE_DIR_RAYLIB=/path/to/raylib).
# -DCSHVH_HEADLESS=ON builds the software-rendered, windowless test binary.

option(CSHVH_HEADLESS "Software renderer without a window (automated tests/screenshots)" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
if(CSHVH_HEADLESS)
  set(PLATFORM "Memory" CACHE STRING "" FORCE)
endif()
FetchContent_Declare(raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG 6.2
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(raylib)

file(GLOB CSHVH_SOURCES CONFIGURE_DEPENDS
  src/engine/*.cpp src/game/shared/*.cpp src/game/server/*.cpp src/game/client/*.cpp
  src/vgui/*.cpp src/cheat/*.cpp src/main.cpp)

add_executable(cshvh ${CSHVH_SOURCES})
target_link_libraries(cshvh PRIVATE raylib)
if(WIN32)
  target_link_libraries(cshvh PRIVATE winhttp)
  target_sources(cshvh PRIVATE res/cshvh.rc)   # exe icon
endif()
target_include_directories(cshvh PRIVATE ${raylib_SOURCE_DIR}/src)

if(CSHVH_HEADLESS)
  target_compile_definitions(cshvh PRIVATE CS_HEADLESS)
  set_target_properties(cshvh PROPERTIES OUTPUT_NAME cshvh_headless)
endif()

if(MSVC)
  target_compile_definitions(cshvh PRIVATE _USE_MATH_DEFINES NOMINMAX _CRT_SECURE_NO_WARNINGS)
  target_compile_options(cshvh PRIVATE /utf-8 /W1 /bigobj)
  set_property(TARGET cshvh PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  if(NOT CSHVH_HEADLESS)
    set_target_properties(cshvh PROPERTIES WIN32_EXECUTABLE ON LINK_FLAGS "/ENTRY:mainCRTStartup")
  endif()
elseif(WIN32 AND NOT CSHVH_HEADLESS)
  target_link_options(cshvh PRIVATE -mwindows -static)
endif()

# put the binary next to assets/ so it can be started by double click
set_target_properties(cshvh PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/bin")
