cmake_minimum_required(VERSION 3.20)
project(particle_sim CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Step 1: Download SFML with FetchContent ────────────────────────────────────
include(FetchContent)

FetchContent_Declare(
    SFML
    GIT_REPOSITORY https://github.com/SFML/SFML.git
    GIT_TAG        2.6.1
    GIT_SHALLOW    TRUE
)

set(SFML_BUILD_AUDIO   OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_NETWORK OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(SFML)

# ── Step 2: Executable ────────────────────────────────────────────────────────
add_executable(particle_sim
    src/main.cpp
    src/ParticleSystem.cpp
)

target_include_directories(particle_sim PRIVATE include)

target_link_libraries(particle_sim PRIVATE
    sfml-graphics
    sfml-window
    sfml-system
)

# ── Step 3: Optional TBB for std::execution::par_unseq ────────────────────────
#
# std::execution::par_unseq (C++17 Parallel STL) needs a backend scheduler:
#
#   Platform    Compiler   Backend       Action needed
#   ─────────── ────────── ─────────── ──────────────────────────────────────
#   Linux       GCC/Clang  Intel TBB   find_package(TBB) + link TBB::tbb
#   macOS       AppleClang none        NOT supported → sequential fallback
#   Windows     MSVC       ConcRT      built into CRT, no extra library
#
# We define PARTICLE_PARALLEL only when the backend is available, so
# ParticleSystem.cpp selects the right execution policy at compile time:
#
#   #ifdef PARTICLE_PARALLEL
#     std::for_each(std::execution::par_unseq, ...)   // multi-core
#   #else
#     std::for_each(                           ...)   // single-core fallback
#   #endif
#
if(MSVC)
    # MSVC ships its own parallel STL — no extra library.
    target_compile_definitions(particle_sim PRIVATE PARTICLE_PARALLEL)
    message(STATUS "[particle_sim] Parallel updates: ENABLED (MSVC/ConcRT)")

elseif(NOT APPLE)
    # GCC and Clang on Linux need TBB.
    # Install on Ubuntu/Debian: sudo apt-get install libtbb-dev
    # Install on Fedora/RHEL:   sudo dnf install tbb-devel
    find_package(TBB QUIET)

    if(TBB_FOUND)
        target_link_libraries(particle_sim PRIVATE TBB::tbb)
        target_compile_definitions(particle_sim PRIVATE PARTICLE_PARALLEL)
        message(STATUS "[particle_sim] Parallel updates: ENABLED (TBB ${TBB_VERSION})")
    else()
        message(STATUS "[particle_sim] Parallel updates: DISABLED (TBB not found — install libtbb-dev)")
    endif()

else()
    # AppleClang does not implement <execution> — sequential fallback.
    message(STATUS "[particle_sim] Parallel updates: DISABLED (AppleClang has no <execution>)")
endif()

# ── Step 4: Compiler warnings and optimisation ────────────────────────────────
target_compile_options(particle_sim PRIVATE
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O2 -Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/O2 /W4>
)

# ── Step 5: Copy SFML DLLs on Windows ────────────────────────────────────────
if(WIN32)
    add_custom_command(TARGET particle_sim POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:particle_sim>
            $<TARGET_FILE_DIR:particle_sim>
        COMMAND_EXPAND_LISTS
    )
endif()
