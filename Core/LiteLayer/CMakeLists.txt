cmake_minimum_required(VERSION 3.21)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    project(BabylonLite LANGUAGES C CXX)
    include(FetchContent)
    set(BGFX_BUILD_EXAMPLES OFF)
    set(BGFX_BUILD_TOOLS OFF)
    set(BGFX_INSTALL OFF)
    set(BGFX_CONFIG_MULTITHREADED ON)
    FetchContent_Declare(LiteBgfx
        GIT_REPOSITORY https://github.com/BabylonJS/bgfx.cmake.git
        GIT_TAG b53236f4d6dede3252d32b9c11921d0007d705d1
        EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(LiteBgfx)
    target_compile_definitions(bgfx PRIVATE BGFX_CONFIG_RENDERER_WEBGPU=0 BGFX_CONFIG_C99_API=0)
    target_compile_definitions(bx PRIVATE _CRT_SECURE_NO_WARNINGS)
    function(warnings_as_errors target)
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4 /WX)
        else()
            target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
    endfunction()
endif()

option(BABYLON_LITE_ENABLE_STYLE_CHECKS "Enforce LiteLayer formatting, declarations and control-flow braces." OFF)
if(BABYLON_LITE_ENABLE_STYLE_CHECKS)
    if(NOT CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
        message(FATAL_ERROR "LiteLayer semantic style checks require a compilation database; use Ninja or Makefiles.")
    endif()
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    find_program(LITE_CLANG_FORMAT NAMES clang-format-22 clang-format REQUIRED)
    find_program(LITE_CLANG NAMES clang-22 clang REQUIRED)
    find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
endif()

file(GLOB LITE_LAYER_SOURCES CONFIGURE_DEPENDS "Source/*.cpp")
file(GLOB LITE_LAYER_PRIVATE_HEADERS CONFIGURE_DEPENDS "Source/*.h")
if(NOT LITE_LAYER_SOURCES)
    message(FATAL_ERROR "LiteLayer requires its handwritten Source/*.cpp implementation.")
endif()

set(SOURCES "Include/babylon_lite.h" ${LITE_LAYER_SOURCES} ${LITE_LAYER_PRIVATE_HEADERS})
add_library(LiteLayer STATIC ${SOURCES})
add_library(Babylon::LiteLayer ALIAS LiteLayer)
warnings_as_errors(LiteLayer)
target_include_directories(LiteLayer PUBLIC "Include")
target_compile_features(LiteLayer PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(LiteLayer PRIVATE /Zc:__cplusplus /Zc:preprocessor)
endif()
target_link_libraries(LiteLayer PRIVATE bgfx bx)
set_property(TARGET LiteLayer PROPERTY FOLDER Core)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})

get_target_property(LITE_LAYER_DIRECT_DEPENDENCIES LiteLayer LINK_LIBRARIES)
if(NOT LITE_LAYER_DIRECT_DEPENDENCIES STREQUAL "bgfx;bx")
    message(FATAL_ERROR "LiteLayer must have only bgfx and bx as direct dependencies.")
endif()

if(BABYLON_LITE_ENABLE_STYLE_CHECKS)
    add_custom_target(LiteLayerStyleCheck
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/Tools/check_style.py"
            --root "${CMAKE_CURRENT_SOURCE_DIR}"
            --database "${CMAKE_BINARY_DIR}/compile_commands.json"
            --clang "${LITE_CLANG}"
            --clang-format "${LITE_CLANG_FORMAT}"
        COMMAND "${CMAKE_COMMAND}" -E env "LITE_STYLE_CLANG=${LITE_CLANG}"
            "${Python3_EXECUTABLE}" -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}/Tools"
        VERBATIM)
    set_property(TARGET LiteLayerStyleCheck PROPERTY FOLDER Core)
    add_dependencies(LiteLayer LiteLayerStyleCheck)
endif()
