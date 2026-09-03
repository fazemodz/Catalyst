cmake_minimum_required(VERSION 3.21)
project(Catalyst LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Catalyst targets Windows (DirectX 12) only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Only the engine (editor) target is wired up here. CatalystPlayer,
# Catalyst.Native and the C# Launcher stay MSBuild-only for now (see
# Catalyst.slnx) and are intentionally not added as subdirectories.
add_subdirectory(Catalyst)
