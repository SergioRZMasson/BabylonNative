cmake_minimum_required(VERSION 3.21)
option(BABYLON_LITE_ENABLE_UI "Build the optional retained RmlUI/bgfx LiteLayer feature." OFF)
option(BABYLON_NATIVE_BUILD_LITE_UI_TESTS "Build the retained UI GPU and lifecycle tests." OFF)

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
    if(BABYLON_LITE_ENABLE_UI)
        function(FetchContent_MakeAvailable_With_Message dependency)
            message(STATUS "Making ${dependency} available for LiteLayer UI")
            FetchContent_MakeAvailable(${dependency})
        endfunction()
        FetchContent_Declare(RmlUi
            GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
            GIT_TAG b7b4a0688262832eacf3b9abb41f8bbe73868af8
            EXCLUDE_FROM_ALL)
        FetchContent_Declare(LiteFreetype
            GIT_REPOSITORY https://github.com/freetype/freetype.git
            GIT_TAG 42608f77f20749dd6ddc9e0536788eaad70ea4b5
            EXCLUDE_FROM_ALL)
        include("${CMAKE_CURRENT_SOURCE_DIR}/../../Dependencies/RmlUi.cmake")
    endif()

    target_compile_definitions(bgfx PRIVATE BGFX_CONFIG_RENDERER_WEBGPU=0 BGFX_CONFIG_C99_API=0)
    if(WIN32 AND BABYLON_LITE_ENABLE_UI)
        target_compile_definitions(bgfx PRIVATE BGFX_CONFIG_RENDERER_DIRECT3D11=1)
    endif()
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
    foreach(LITE_STYLE_TOOL IN ITEMS LITE_CLANG_FORMAT LITE_CLANG)
        execute_process(COMMAND "${${LITE_STYLE_TOOL}}" --version
            OUTPUT_VARIABLE LITE_STYLE_VERSION RESULT_VARIABLE LITE_STYLE_RESULT)
        if(NOT LITE_STYLE_RESULT EQUAL 0 OR NOT LITE_STYLE_VERSION MATCHES "version 22\\.")
            message(FATAL_ERROR "${LITE_STYLE_TOOL} must select LLVM 22; set it to the pinned tool's full path.")
        endif()
    endforeach()
    find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
endif()

file(GLOB LITE_LAYER_SOURCES CONFIGURE_DEPENDS "Source/*.cpp")
file(GLOB LITE_LAYER_PRIVATE_HEADERS CONFIGURE_DEPENDS "Source/*.h")
if(NOT BABYLON_LITE_ENABLE_UI)
    list(FILTER LITE_LAYER_SOURCES EXCLUDE REGEX "/Ui[^/]*\\.cpp$")
    list(APPEND LITE_LAYER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Source/UiDisabled.cpp")
endif()
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
if(BABYLON_LITE_ENABLE_UI)
    list(REMOVE_ITEM LITE_LAYER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Source/UiDisabled.cpp")
    set_property(TARGET LiteLayer PROPERTY SOURCES
        "Include/babylon_lite.h" ${LITE_LAYER_SOURCES} ${LITE_LAYER_PRIVATE_HEADERS})
    target_link_libraries(LiteLayer PRIVATE RmlUi::Core)
    target_compile_definitions(LiteLayer PRIVATE BL_UI_ENABLED=1)
    set_source_files_properties("Source/UiRmlAdapter.cpp" PROPERTIES
        COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")
    target_include_directories(LiteLayer SYSTEM PRIVATE "${RmlUi_SOURCE_DIR}/Include")
endif()
set_property(TARGET LiteLayer PROPERTY FOLDER Core)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})

get_target_property(LITE_LAYER_DIRECT_DEPENDENCIES LiteLayer LINK_LIBRARIES)
set(LITE_LAYER_ALLOWED_DEPENDENCIES "bgfx;bx")
if(BABYLON_LITE_ENABLE_UI)
    list(APPEND LITE_LAYER_ALLOWED_DEPENDENCIES RmlUi::Core)
endif()
if(NOT LITE_LAYER_DIRECT_DEPENDENCIES STREQUAL LITE_LAYER_ALLOWED_DEPENDENCIES)
    message(FATAL_ERROR "LiteLayer direct dependencies must match the approved feature targets.")
endif()

if(BABYLON_LITE_ENABLE_STYLE_CHECKS)
    set(LITE_STYLE_FEATURE_ARGUMENTS)
    if(BABYLON_LITE_ENABLE_UI)
        list(APPEND LITE_STYLE_FEATURE_ARGUMENTS --ui-enabled)
    endif()
    add_custom_target(LiteLayerStyleCheck
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/Tools/check_style.py"
            --root "${CMAKE_CURRENT_SOURCE_DIR}"
            --database "${CMAKE_BINARY_DIR}/compile_commands.json"
            --clang "${LITE_CLANG}"
            --clang-format "${LITE_CLANG_FORMAT}"
            ${LITE_STYLE_FEATURE_ARGUMENTS}
        COMMAND "${CMAKE_COMMAND}" -E env "LITE_STYLE_CLANG=${LITE_CLANG}"
            "LITE_STYLE_FIXTURE_ROOT=${CMAKE_BINARY_DIR}/StyleFixtures"
            "${Python3_EXECUTABLE}" -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}/Tools"
        VERBATIM)
    set_property(TARGET LiteLayerStyleCheck PROPERTY FOLDER Core)
    add_dependencies(LiteLayer LiteLayerStyleCheck)
endif()

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR AND BABYLON_NATIVE_BUILD_LITE_UI_TESTS)
    if(NOT BABYLON_LITE_ENABLE_UI)
        message(FATAL_ERROR "Standalone UI tests require BABYLON_LITE_ENABLE_UI=ON.")
    endif()
    enable_testing()
    add_subdirectory("../../Apps/LiteTests/UiTests" UiTests)
endif()
