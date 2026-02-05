cmake_minimum_required(VERSION 3.16)

find_package(Qt6 COMPONENTS Widgets REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

add_executable(ply2lcc-gui
    main.cpp
    mainwindow.cpp
    mainwindow.hpp
    convertworker.cpp
    convertworker.hpp
)

target_link_libraries(ply2lcc-gui PRIVATE
    Qt6::Widgets
    ply2lcc_lib
)

target_include_directories(ply2lcc-gui PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
