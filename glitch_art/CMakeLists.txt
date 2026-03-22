cmake_minimum_required(VERSION 3.20)
project(glitch_art CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── stb (single-header image I/O library) ─────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
    FetchContent_Populate(stb)
endif()

# ── glitch_art executable ──────────────────────────────────────────────────────
add_executable(glitch_art
    src/main.cpp
    src/stb_impl.cpp   # defines STB_IMAGE_IMPLEMENTATION / STB_IMAGE_WRITE_IMPLEMENTATION
)

target_include_directories(glitch_art PRIVATE include)
# Mark stb as a system include so compiler warnings in the vendored
# single-header library are silenced (they are not our code to fix).
target_include_directories(glitch_art SYSTEM PRIVATE ${stb_SOURCE_DIR})

target_compile_options(glitch_art PRIVATE
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O2 -Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)
