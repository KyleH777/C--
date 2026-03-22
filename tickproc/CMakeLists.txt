cmake_minimum_required(VERSION 3.20)
project(tickproc VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Compiler flags ──────────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(/W4)
    # Release-only MSVC flags are handled by CMAKE_BUILD_TYPE defaults
else()
    add_compile_options(-Wall -Wextra -Wpedantic)

    # Release-only optimizations (don't override Debug's -O0 -g)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3)

        option(TICKPROC_ENABLE_LTO "Enable link-time optimization" ON)
        if(TICKPROC_ENABLE_LTO)
            add_compile_options(-flto)
            add_link_options(-flto)
        endif()

        # -march=native is optimal for local builds but breaks CI/cross-compile.
        option(TICKPROC_NATIVE_ARCH "Use -march=native for host-specific tuning" OFF)
        if(TICKPROC_NATIVE_ARCH)
            add_compile_options(-march=native)
        endif()
    endif()
endif()

# ── Library target (core logic, reusable in tests) ──────────────────────────
add_library(tickproc_core STATIC
    src/mapped_file.cpp
    src/csv_parser.cpp
)
target_include_directories(tickproc_core PUBLIC include)
# Platform-specific link dependencies
if(UNIX)
    target_link_libraries(tickproc_core PUBLIC pthread)
endif()

# ── CLI executable ──────────────────────────────────────────────────────────
add_executable(tickproc src/main.cpp)
target_link_libraries(tickproc PRIVATE tickproc_core)

# ── Tests ───────────────────────────────────────────────────────────────────
option(TICKPROC_BUILD_TESTS "Build unit tests" ON)
if(TICKPROC_BUILD_TESTS)
    enable_testing()

    add_executable(test_parser   tests/test_parser.cpp)
    target_link_libraries(test_parser PRIVATE tickproc_core)
    add_test(NAME test_parser COMMAND test_parser)

    add_executable(test_queue    tests/test_queue.cpp)
    target_link_libraries(test_queue PRIVATE tickproc_core)
    add_test(NAME test_queue COMMAND test_queue)

    add_executable(test_data_processor tests/test_data_processor.cpp)
    target_link_libraries(test_data_processor PRIVATE tickproc_core)
    add_test(NAME test_data_processor COMMAND test_data_processor)
endif()
