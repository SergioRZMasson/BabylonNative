cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(BabylonLiteUiTests LANGUAGES C CXX)
    enable_testing()
    set(BABYLON_LITE_ENABLE_UI ON CACHE BOOL "" FORCE)
    add_subdirectory("../../../Core/LiteLayer" LiteLayer)
endif()
if(NOT TARGET LiteShaderCompiler)
    set(LITE_UI_COMPILER_LIBRARY_DIR "" CACHE PATH "Exact-pin Release compiler library build.")
    if(NOT LITE_UI_COMPILER_LIBRARY_DIR)
        message(FATAL_ERROR "Standalone UI tests require LITE_UI_COMPILER_LIBRARY_DIR.")
    endif()
    set(_compiler_root "${LITE_UI_COMPILER_LIBRARY_DIR}")
    add_library(LiteShaderCompiler INTERFACE)
    target_include_directories(LiteShaderCompiler INTERFACE
        "../../../Core/LiteShaderCompiler/Include")
    file(GLOB_RECURSE _compiler_libs
        "${_compiler_root}/LiteShaderCompiler.lib"
        "${_compiler_root}/LiteShaderCompilerBackend.lib"
        "${_compiler_root}/LiteSpirvCross.lib")
    set(LITE_UI_TINT_LIBRARY_DIR "" CACHE PATH "Exact-pin Release Tint libraries.")
    file(GLOB_RECURSE _tint_libs "${LITE_UI_TINT_LIBRARY_DIR}/*tint*.lib"
        "${LITE_UI_TINT_LIBRARY_DIR}/*absl*.lib"
        "${LITE_UI_TINT_LIBRARY_DIR}/*SPIRV-Tools*.lib")
    if(NOT _compiler_libs OR NOT _tint_libs)
        message(FATAL_ERROR "UI tests need the actual existing compiler and Tint libraries.")
    endif()
    target_link_libraries(LiteShaderCompiler INTERFACE ${_compiler_libs} ${_tint_libs}
        d3dcompiler)
endif()

if(NOT WIN32 OR WINDOWS_STORE)
    message(FATAL_ERROR "The current UI pixel qualification host is Win32/D3D11 only.")
endif()
add_library(LiteUiHeaderC99 OBJECT "ContractC99.c")
target_link_libraries(LiteUiHeaderC99 PRIVATE LiteLayer)
set_target_properties(LiteUiHeaderC99 PROPERTIES C_STANDARD 99 C_EXTENSIONS OFF)
warnings_as_errors(LiteUiHeaderC99)
add_executable(LiteUiTests "UiTests.cpp")
target_link_libraries(LiteUiTests PRIVATE LiteLayer LiteShaderCompiler bgfx user32 psapi)
target_link_libraries(LiteUiTests PRIVATE RmlUi::Core)
target_include_directories(LiteUiTests PRIVATE "../../../Core/LiteLayer/Source")
target_compile_features(LiteUiTests PRIVATE cxx_std_20)
target_compile_definitions(LiteUiTests PRIVATE _CRT_SECURE_NO_WARNINGS
    LITE_UI_FONT_FILE="${RmlUi_SOURCE_DIR}/Samples/assets/LatoLatin-Regular.ttf")
if(MSVC)
    target_link_options(LiteUiTests PRIVATE "/MAP:${CMAKE_CURRENT_BINARY_DIR}/LiteUiTests.map")
endif()
warnings_as_errors(LiteUiTests)
add_test(NAME LiteUiTests COMMAND LiteUiTests)
set_tests_properties(LiteUiTests PROPERTIES RUN_SERIAL TRUE)
set_property(TARGET LiteUiTests PROPERTY FOLDER Apps)
find_package(Python3 3.9 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME LiteUiRmlPatchTests
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/TestRmlPatch.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../.."
            "${RmlUi_SOURCE_DIR}"
            "${CMAKE_CURRENT_BINARY_DIR}/PatchFixture")
endif()

option(LITE_UI_CORE_REGRESSION_TESTS "Recompile original Core regressions in this new UI build." OFF)
set(LITE_UI_GTEST_SOURCE_DIR "" CACHE PATH "Existing source-pinned GoogleTest source directory.")
set(LITE_UI_GTEST_LIBRARY_DIR "" CACHE PATH "Existing exact-compiler Release GoogleTest libraries.")
if(LITE_UI_CORE_REGRESSION_TESTS)
    if(NOT TARGET gtest_main)
        if(NOT EXISTS "${LITE_UI_GTEST_SOURCE_DIR}/googletest/include/gtest/gtest.h" OR
            NOT EXISTS "${LITE_UI_GTEST_LIBRARY_DIR}/gtest.lib" OR
            NOT EXISTS "${LITE_UI_GTEST_LIBRARY_DIR}/gtest_main.lib")
            message(FATAL_ERROR "Core regression tests need the existing source/library GoogleTest cache.")
        endif()
        add_library(LiteUiGtest INTERFACE)
        target_include_directories(LiteUiGtest SYSTEM INTERFACE
            "${LITE_UI_GTEST_SOURCE_DIR}/googletest/include")
        target_link_libraries(LiteUiGtest INTERFACE
            "${LITE_UI_GTEST_LIBRARY_DIR}/gtest_main.lib"
            "${LITE_UI_GTEST_LIBRARY_DIR}/gtest.lib")
        set(_ui_gtest LiteUiGtest)
    else()
        set(_ui_gtest gtest_main)
    endif()
    add_executable(LiteUiCoreRegressionTests
        "../NativeTests/Source/Tests.Geometry.cpp"
        "../NativeTests/Source/Tests.Nodes.cpp"
        "../NativeTests/Source/Tests.ArcLight.cpp"
        "../NativeTests/Source/Tests.Audio.cpp"
        "../NativeTests/Source/Tests.RenderOrder.cpp"
        "../NativeTests/Source/Tests.Runtime.cpp")
    target_link_libraries(LiteUiCoreRegressionTests PRIVATE LiteLayer ${_ui_gtest} bgfx bx)
    target_include_directories(LiteUiCoreRegressionTests PRIVATE "../../../Core/LiteLayer/Source")
    target_compile_features(LiteUiCoreRegressionTests PRIVATE cxx_std_20)
    target_compile_definitions(LiteUiCoreRegressionTests PRIVATE _CRT_SECURE_NO_WARNINGS
        BL_ORACLE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/../NativeTests/Fixtures/OfficialLite")
    warnings_as_errors(LiteUiCoreRegressionTests)
    add_test(NAME LiteUiCoreRegressionTests COMMAND LiteUiCoreRegressionTests)

    add_executable(LiteUiGraphicsRegressionTests "../NativeTests/Source/Tests.Graphics.cpp")
    target_link_libraries(LiteUiGraphicsRegressionTests PRIVATE
        LiteLayer LiteShaderCompiler ${_ui_gtest} bgfx bx user32)
    target_include_directories(LiteUiGraphicsRegressionTests PRIVATE "../../../Core/LiteLayer/Source")
    target_compile_features(LiteUiGraphicsRegressionTests PRIVATE cxx_std_20)
    warnings_as_errors(LiteUiGraphicsRegressionTests)
    add_test(NAME LiteUiGraphicsRegressionTests COMMAND LiteUiGraphicsRegressionTests)
    set_tests_properties(LiteUiGraphicsRegressionTests PROPERTIES RUN_SERIAL TRUE)

    add_executable(LiteUiImplementationRegression "../NativeTests/Source/ImplementationCpuTests.cpp")
    target_link_libraries(LiteUiImplementationRegression PRIVATE LiteLayer)
    target_compile_features(LiteUiImplementationRegression PRIVATE cxx_std_20)
    target_compile_options(LiteUiImplementationRegression PRIVATE /UNDEBUG)
    target_compile_definitions(LiteUiImplementationRegression PRIVATE _CRT_SECURE_NO_WARNINGS)
    warnings_as_errors(LiteUiImplementationRegression)
    add_test(NAME LiteUiImplementationRegression COMMAND LiteUiImplementationRegression)
endif()

add_executable(LiteUiDataOnly "../NativeTests/Source/DataOnlyCheck.cpp")
target_link_libraries(LiteUiDataOnly PRIVATE LiteLayer)
target_compile_options(LiteUiDataOnly PRIVATE /UNDEBUG)
warnings_as_errors(LiteUiDataOnly)
target_link_options(LiteUiDataOnly PRIVATE "/MAP:${CMAKE_CURRENT_BINARY_DIR}/LiteUiDataOnly.map")
add_custom_command(TARGET LiteUiDataOnly POST_BUILD
    COMMAND "${CMAKE_COMMAND}" "-DMAP=${CMAKE_CURRENT_BINARY_DIR}/LiteUiDataOnly.map"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/CheckDataOnly.cmake"
    VERBATIM)
add_test(NAME LiteUiDataOnly COMMAND LiteUiDataOnly)
