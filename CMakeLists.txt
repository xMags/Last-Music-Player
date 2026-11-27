cmake_minimum_required(VERSION 3.25)

project(LastMusicPlayer
    VERSION 2.0.0
    DESCRIPTION "Last Music Player"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Concurrent Gui Network Qml Quick QuickControls2 ShaderTools Svg Test WebSockets)
qt_standard_project_setup(REQUIRES 6.8)

include(CompilerWarnings)
include(PrivateConfig)

add_subdirectory(third_party/sqlite)
add_subdirectory(third_party/taglib)
add_subdirectory(src/core)
add_subdirectory(src/platform/windows)
add_subdirectory(src/ui)
add_subdirectory(src/app)

enable_testing()
add_subdirectory(tests)
