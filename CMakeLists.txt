cmake_minimum_required(VERSION 3.16)
project(ply2lcc VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(ply2lcc
    src/main.cpp
    src/ply_reader.cpp
    src/compression.cpp
    src/lcc_writer.cpp
    src/spatial_grid.cpp
    src/meta_writer.cpp
    external/miniply/miniply.cpp
)

target_include_directories(ply2lcc PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external
)

target_compile_options(ply2lcc PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -O2>
)

# Testing
option(BUILD_TESTS "Build unit tests" ON)

if(BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    # Create a library for the converter code (to link with tests)
    add_library(ply2lcc_lib STATIC
        src/ply_reader.cpp
        src/compression.cpp
        src/lcc_writer.cpp
        src/spatial_grid.cpp
        src/meta_writer.cpp
        external/miniply/miniply.cpp
    )
    target_include_directories(ply2lcc_lib PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/external
    )

    # Unit tests
    add_executable(test_compression tests/test_compression.cpp)
    target_link_libraries(test_compression ply2lcc_lib GTest::gtest_main)

    add_executable(test_types tests/test_types.cpp)
    target_link_libraries(test_types ply2lcc_lib GTest::gtest_main)

    # Integration tests
    add_executable(test_integration tests/test_integration.cpp)
    target_link_libraries(test_integration ply2lcc_lib GTest::gtest_main)

    # Path resolution tests
    add_executable(test_path_resolution tests/test_path_resolution.cpp)
    target_link_libraries(test_path_resolution ply2lcc_lib GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(test_compression)
    gtest_discover_tests(test_types)
    gtest_discover_tests(test_integration)
    gtest_discover_tests(test_path_resolution)
endif()
