cmake_minimum_required(VERSION 3.16)
project(ply2lcc VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(OpenMP REQUIRED)

add_executable(ply2lcc
    src/main.cpp
    src/convert_app.cpp
    src/splat_buffer.cpp
    src/ply_reader_mmap.cpp
    src/compression.cpp
    src/spatial_grid.cpp
    src/meta_writer.cpp
    src/env_writer.cpp
    src/attrs_writer.cpp
    external/miniply/miniply.cpp
)

target_include_directories(ply2lcc PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external
)

target_compile_options(ply2lcc PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -O2>
    $<$<CXX_COMPILER_ID:MSVC>:/W3 /O2>
)

# Windows-specific: prevent min/max macros from conflicting with std::min/std::max
if(MSVC)
    target_compile_definitions(ply2lcc PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()

target_link_libraries(ply2lcc PRIVATE OpenMP::OpenMP_CXX)

# Options
option(BUILD_TESTS "Build unit tests" ON)
option(BUILD_GUI "Build Qt GUI application" OFF)

# Build shared library if tests or GUI are enabled
if(BUILD_TESTS OR BUILD_GUI)
    add_library(ply2lcc_lib STATIC
        src/convert_app.cpp
        src/splat_buffer.cpp
        src/ply_reader_mmap.cpp
        src/compression.cpp
        src/spatial_grid.cpp
        src/meta_writer.cpp
        src/env_writer.cpp
        src/attrs_writer.cpp
        external/miniply/miniply.cpp
    )
    target_include_directories(ply2lcc_lib PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/external
    )
    target_link_libraries(ply2lcc_lib PUBLIC OpenMP::OpenMP_CXX)
    if(MSVC)
        target_compile_definitions(ply2lcc_lib PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
    endif()
endif()

# Testing
if(BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    # Unit tests
    add_executable(test_compression tests/test_compression.cpp)
    target_link_libraries(test_compression ply2lcc_lib GTest::gtest_main)

    add_executable(test_types tests/test_types.cpp)
    target_link_libraries(test_types ply2lcc_lib GTest::gtest_main)

    # Integration tests
    add_executable(test_integration tests/test_integration.cpp)
    target_link_libraries(test_integration ply2lcc_lib GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(test_compression)
    gtest_discover_tests(test_types)
    gtest_discover_tests(test_integration)
endif()

# GUI
if(BUILD_GUI)
    add_subdirectory(gui)
endif()
