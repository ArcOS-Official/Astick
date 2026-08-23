cmake_minimum_required(VERSION 3.6)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_BUILD_TYPE "Debug")

project(Astick
    VERSION 0.1
    DESCRIPTION "Compositor for ArcOS (ArcDE)")

# Embedded config header generation from config.template.json
# Hand-written src/embedded_config.h is committed; this keeps it in sync when Python is available.
find_package(Python3 QUIET)
if(Python3_FOUND)
    add_custom_command(
        OUTPUT ${CMAKE_SOURCE_DIR}/src/embedded_config.h
        COMMAND ${CMAKE_COMMAND} -E echo "/* generated from config.template.json - do not edit manually, regenerate via CMake or python3 */" > ${CMAKE_SOURCE_DIR}/src/embedded_config.h
        COMMAND ${CMAKE_COMMAND} -E echo "#pragma once" >> ${CMAKE_SOURCE_DIR}/src/embedded_config.h
        COMMAND ${CMAKE_COMMAND} -E echo "" >> ${CMAKE_SOURCE_DIR}/src/embedded_config.h
        COMMAND Python3::Interpreter -c "import pathlib; data=pathlib.Path('${CMAKE_SOURCE_DIR}/config.template.json').read_text(); print('constexpr const char kEmbeddedConfig[] = R\"JSON(' + data + ')JSON\";')" >> ${CMAKE_SOURCE_DIR}/src/embedded_config.h
        DEPENDS ${CMAKE_SOURCE_DIR}/config.template.json
        COMMENT "Generating src/embedded_config.h from config.template.json"
        VERBATIM
    )
    add_custom_target(embedded_config_header DEPENDS ${CMAKE_SOURCE_DIR}/src/embedded_config.h)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui)
find_package(PkgConfig REQUIRED)
pkg_check_modules(Wlroots REQUIRED IMPORTED_TARGET wlroots-0.19)
pkg_check_modules(WaylandServer REQUIRED IMPORTED_TARGET wayland-server)
pkg_check_modules(WaylandClient REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(Xkbcommon REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(Libinput REQUIRED IMPORTED_TARGET libinput)
find_package(nlohmann_json 3.2.0 QUIET)
if(NOT nlohmann_json_FOUND)
    # header-only fallback, still need include path
    message(STATUS "nlohmann_json not found via CMake, using header-only")
endif()
add_compile_options(-Wall -Wextra -DWLR_USE_UNSTABLE -Wno-vla)

set(SOURCES
    src/main.cpp
    src/toplevel.cpp
    src/popup.cpp
    src/layersurface.cpp
    src/cursor.cpp
    src/layout.cpp
    src/output.cpp
    src/debug.cpp
    src/application.cpp
    src/util.cpp
    src/config.cpp
    src/animation.cpp
    src/animation_pool.cpp
    src/resource.cpp
    src/decoration.cpp
    src/input/keyboard.cpp
    src/input/mouse.cpp
    src/state/state.cpp
    src/state/keyboard_state.cpp
    src/state/pointer_state.cpp
    src/state/animation_manager.cpp
    src/engine/engine.cpp
    src/input/input_manager.cpp
    src/output/output_manager.cpp
)
add_executable(Astick
    ${SOURCES}
)

target_link_libraries(Astick PRIVATE Qt6::Core Qt6::Gui)
target_link_libraries(Astick PRIVATE PkgConfig::Wlroots)
target_link_libraries(Astick PRIVATE PkgConfig::WaylandServer)
target_link_libraries(Astick PRIVATE PkgConfig::WaylandClient)
target_link_libraries(Astick PRIVATE PkgConfig::Xkbcommon)
if(TARGET PkgConfig::Libinput)
    target_link_libraries(Astick PRIVATE PkgConfig::Libinput)
endif()
if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(Astick PRIVATE nlohmann_json::nlohmann_json)
endif()
target_include_directories(Astick PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/protocols")

if(TARGET embedded_config_header)
    add_dependencies(Astick embedded_config_header)
endif()
