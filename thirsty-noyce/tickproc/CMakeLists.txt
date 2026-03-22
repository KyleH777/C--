cmake_minimum_required(VERSION 3.20)
project(tickproc VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Library target ───────────────────────────────────────────────────────────
add_library(tickproc_core STATIC
    src/mapped_file.cpp
    src/csv_parser.cpp
)
target_include_directories(tickproc_core PUBLIC include)

find_package(Threads REQUIRED)
target_link_libraries(tickproc_core PUBLIC Threads::Threads)

# ── CLI executable ───────────────────────────────────────────────────────────
add_executable(tickproc src/main.cpp)
target_link_libraries(tickproc PRIVATE tickproc_core)

# ── Tests ────────────────────────────────────────────────────────────────────
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
