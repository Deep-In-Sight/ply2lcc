cmake_minimum_required(VERSION 3.16)

# Try Qt6 first, fall back to Qt5
find_package(Qt6 COMPONENTS Widgets QUIET)
if(NOT Qt6_FOUND)
    find_package(Qt5 COMPONENTS Widgets REQUIRED)
    set(QT_VERSION_MAJOR 5)
else()
    set(QT_VERSION_MAJOR 6)
endif()

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

# Hide console window on Windows
if(WIN32)
    set_target_properties(ply2lcc-gui PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_link_libraries(ply2lcc-gui PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    ply2lcc_lib
)

target_include_directories(ply2lcc-gui PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
