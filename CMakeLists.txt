cmake_minimum_required(VERSION 3.6)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_BUILD_TYPE "Debug")

project(Astick
    VERSION 0.1
    DESCRIPTION "Compositor for ArcOS (ArcDE)")

find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(PkgConfig REQUIRED)
pkg_check_modules(Wlroots REQUIRED IMPORTED_TARGET wlroots-0.19)
pkg_check_modules(WaylandServer REQUIRED IMPORTED_TARGET wayland-server)
pkg_check_modules(WaylandClient REQUIRED IMPORTED_TARGET wayland-client)
add_compile_options(-Wall -Wextra -DWLR_USE_UNSTABLE -Wno-vla)

set(SOURCES
    src/main.cpp
    src/compositor.cpp
    src/compositor.h
    src/wlroots.h
    src/output.h
    src/output.cpp
    src/debug.cpp
    src/debug.h
    src/application.h
    src/application.cpp
    src/util.h
    src/util.cpp
    src/input/keyboard.cpp
    src/input/keyboard.h
    src/input/mouse.cpp
    src/input/mouse.h
)
add_executable(Astick
    ${SOURCES}
)

target_link_libraries(Astick PRIVATE Qt6::Core)
target_link_libraries(Astick PRIVATE PkgConfig::Wlroots)
target_link_libraries(Astick PRIVATE PkgConfig::WaylandServer)
target_link_libraries(Astick PRIVATE PkgConfig::WaylandClient)
target_include_directories(Astick PRIVATE "/usr/include/wlroots-0.19/")
