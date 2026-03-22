cmake_minimum_required(VERSION 3.20)
project(particle_sim CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Step 1: Download SFML with FetchContent ────────────────────────────────────
# FetchContent pulls a library from a remote git repo and builds it from source
# as part of *your* build. No system install required.
#
# Why this beats find_package() for cross-platform work:
#   • find_package() relies on the library already being installed on the host.
#     This fails on fresh CI runners and on any machine where SFML isn't in a
#     standard system path.
#   • FetchContent pins an exact version (GIT_TAG) so every developer and every
#     CI runner builds against the same SFML revision — no "works on my machine"
#     version skew.
#   • Works identically on Linux, macOS, and Windows with no extra steps.
#
include(FetchContent)

FetchContent_Declare(
    SFML
    GIT_REPOSITORY https://github.com/SFML/SFML.git
    GIT_TAG        2.6.1        # pinned release — change to upgrade
    GIT_SHALLOW    TRUE         # only clone the tagged commit, not full history
)

# ── Step 2: Disable SFML modules we don't need ────────────────────────────────
# This cuts compile time significantly — sfml-audio alone pulls in OpenAL and
# several codec libraries. We only need graphics + window + system.
set(SFML_BUILD_AUDIO   OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_NETWORK OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(SFML)

# ── Step 3: Define the executable ─────────────────────────────────────────────
add_executable(particle_sim
    src/main.cpp
    src/ParticleSystem.cpp
)

target_include_directories(particle_sim PRIVATE include)

# ── Step 4: Link SFML ─────────────────────────────────────────────────────────
# SFML 2.x targets use lowercase names when built from source via FetchContent.
# (SFML 3.x switches to SFML::Graphics style — update GIT_TAG and these names
#  together if you upgrade.)
#
# PRIVATE means SFML headers are an implementation detail of particle_sim and
# won't leak into any downstream targets that link particle_sim.
target_link_libraries(particle_sim PRIVATE
    sfml-graphics   # sf::RenderWindow, sf::VertexArray, sf::Color …
    sfml-window     # sf::Window, sf::Event, sf::Mouse …
    sfml-system     # sf::Vector2f, sf::Clock, sf::Time …
)

# ── Step 5: Compiler warnings and optimisation ────────────────────────────────
# Use generator expressions to emit the right flags per compiler:
#   GCC/Clang → -O2 -Wall -Wextra -Wpedantic
#   MSVC      → /O2 /W4
target_compile_options(particle_sim PRIVATE
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O2 -Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/O2 /W4>
)

# ── Step 6: Copy SFML DLLs next to the exe on Windows (runtime requirement) ───
# On Windows, SFML is built as DLLs. Without this, the exe won't run because
# Windows searches for DLLs in the same directory as the binary first.
if(WIN32)
    add_custom_command(TARGET particle_sim POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:particle_sim>
            $<TARGET_FILE_DIR:particle_sim>
        COMMAND_EXPAND_LISTS
    )
endif()
