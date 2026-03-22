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

target_include_directories(glitch_art PRIVATE
    include
    ${stb_SOURCE_DIR}
)

target_compile_options(glitch_art PRIVATE
    -O2
    -Wall
    -Wextra
    -Wpedantic
)
