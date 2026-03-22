cmake_minimum_required(VERSION 3.20)
project(sfml_smoke_test CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Download SFML automatically (no system install required) ──────────────────
# This is the same approach used by the rest of this repo.
# On first cmake run it fetches SFML 2.6.1 from GitHub and compiles it.
# Subsequent runs use the cached copy — fast.
include(FetchContent)
FetchContent_Declare(
    SFML
    GIT_REPOSITORY https://github.com/SFML/SFML.git
    GIT_TAG        2.6.1
    GIT_SHALLOW    TRUE       # only this commit — saves bandwidth
)

# Disable SFML modules we don't use (speeds up the first compile significantly)
set(SFML_BUILD_AUDIO   OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_NETWORK OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(SFML)

# ── Build the smoke test executable ──────────────────────────────────────────
add_executable(sfml_smoke_test main.cpp)

target_link_libraries(sfml_smoke_test PRIVATE
    sfml-graphics   # sf::RenderWindow, sf::Color
    sfml-window     # sf::Event, window management
    sfml-system     # sf::Vector2f and other core types
)

# ── Windows only: copy SFML DLLs next to the .exe ────────────────────────────
if(WIN32)
    add_custom_command(TARGET sfml_smoke_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sfml_smoke_test>
            $<TARGET_FILE_DIR:sfml_smoke_test>
        COMMAND_EXPAND_LISTS
    )
endif()
