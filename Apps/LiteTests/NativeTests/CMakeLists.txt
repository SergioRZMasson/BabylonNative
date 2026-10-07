set(SOURCES
    "Source/ContractC99.c"
    "Source/TestRuntime.h"
    "Source/Tests.Geometry.cpp"
    "Source/Tests.Nodes.cpp"
    "Source/Tests.Audio.cpp"
    "Source/Tests.RenderOrder.cpp"
    "Source/Tests.Runtime.cpp")

add_library(LiteHeaderC99 OBJECT "Source/ContractC99.c")
set_property(TARGET LiteHeaderC99 PROPERTY C_STANDARD 99)
set_property(TARGET LiteHeaderC99 PROPERTY C_STANDARD_REQUIRED ON)
set_property(TARGET LiteHeaderC99 PROPERTY C_EXTENSIONS OFF)
target_include_directories(LiteHeaderC99 PRIVATE "${PROJECT_SOURCE_DIR}/Core/LiteLayer/Include")
warnings_as_errors(LiteHeaderC99)

add_executable(LiteNativeTests ${SOURCES})
warnings_as_errors(LiteNativeTests)
target_link_libraries(LiteNativeTests PRIVATE LiteLayer gtest_main)
target_include_directories(LiteNativeTests PRIVATE
    "${PROJECT_SOURCE_DIR}/Core/LiteLayer/Source" "${BGFX_DIR}/include"
    "${BX_DIR}/include")
target_compile_definitions(LiteNativeTests PRIVATE BX_CONFIG_DEBUG=0)
target_compile_features(LiteNativeTests PRIVATE cxx_std_20)
target_compile_definitions(LiteNativeTests PRIVATE
    BL_ORACLE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/Fixtures/OfficialLite")
add_test(NAME LiteNativeTests COMMAND LiteNativeTests)
set_property(TARGET LiteNativeTests PROPERTY FOLDER Apps)
set_property(TARGET LiteHeaderC99 PROPERTY FOLDER Apps)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})

if(TARGET LiteShaderCompiler AND WIN32 AND NOT WINDOWS_STORE)
    set(GRAPHICS_SOURCES "Source/Tests.Graphics.cpp" "Source/TestRuntime.h")
    add_executable(LiteNativeGraphicsTests ${GRAPHICS_SOURCES})
    warnings_as_errors(LiteNativeGraphicsTests)
    target_link_libraries(LiteNativeGraphicsTests PRIVATE LiteLayer LiteShaderCompiler gtest_main)
    target_include_directories(LiteNativeGraphicsTests PRIVATE "${BGFX_DIR}/include")
    target_include_directories(LiteNativeGraphicsTests PRIVATE
        "${PROJECT_SOURCE_DIR}/Core/LiteLayer/Source" "${BX_DIR}/include")
    target_compile_definitions(LiteNativeGraphicsTests PRIVATE BX_CONFIG_DEBUG=0)
    target_compile_features(LiteNativeGraphicsTests PRIVATE cxx_std_20)
    add_test(NAME LiteNativeGraphicsTests COMMAND LiteNativeGraphicsTests)
    set_property(TARGET LiteNativeGraphicsTests PROPERTY FOLDER Apps)
    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${GRAPHICS_SOURCES})
endif()

if(WIN32 AND NOT WINDOWS_STORE)
    add_executable(LitePlatformAudioTests "Source/Tests.PlatformAudio.cpp"
        "../LitePlayground/Source/NativeAudio.cpp" "../LitePlayground/Source/NativeAudio.h")
    warnings_as_errors(LitePlatformAudioTests)
    target_link_libraries(LitePlatformAudioTests PRIVATE LiteLayer gtest_main xaudio2)
    add_test(NAME LitePlatformAudioTests COMMAND LitePlatformAudioTests)
    set_property(TARGET LitePlatformAudioTests PROPERTY FOLDER Apps)
endif()

set(BABYLON_LITE_IMPLEMENTATION_TEST_SOURCE "" CACHE FILEPATH "Optional independently authored C99 implementation test application.")
if(WIN32)
    add_executable(LiteDataOnlyTests "Source/DataOnlyCheck.cpp")
    warnings_as_errors(LiteDataOnlyTests)
    target_link_libraries(LiteDataOnlyTests PRIVATE LiteLayer)
    if(MSVC)
        target_compile_options(LiteDataOnlyTests PRIVATE /UNDEBUG)
        target_link_options(LiteDataOnlyTests PRIVATE "/MAP:${CMAKE_CURRENT_BINARY_DIR}/LiteDataOnlyTests.map")
        add_custom_command(TARGET LiteDataOnlyTests POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                "-DMAP=${CMAKE_CURRENT_BINARY_DIR}/LiteDataOnlyTests.map"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/Tools/CheckDataOnlyLink.cmake"
            VERBATIM)
    endif()
    add_test(NAME LiteDataOnlyTests COMMAND LiteDataOnlyTests)
    set_property(TARGET LiteDataOnlyTests PROPERTY FOLDER Apps)
    if(BABYLON_LITE_IMPLEMENTATION_TEST_SOURCE)
        set(LITE_IMPLEMENTATION_TEST_SOURCE "${BABYLON_LITE_IMPLEMENTATION_TEST_SOURCE}")
    else()
        set(LITE_IMPLEMENTATION_TEST_SOURCE "Source/ImplementationCpuTests.cpp")
    endif()
    add_executable(LiteImplementationCpuTests "${LITE_IMPLEMENTATION_TEST_SOURCE}")
    warnings_as_errors(LiteImplementationCpuTests)
    target_link_libraries(LiteImplementationCpuTests PRIVATE LiteLayer)
    if(MSVC)
        target_compile_options(LiteImplementationCpuTests PRIVATE /UNDEBUG)
    else()
        target_compile_options(LiteImplementationCpuTests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME LiteImplementationCpuTests COMMAND LiteImplementationCpuTests)
    set_property(TARGET LiteImplementationCpuTests PROPERTY FOLDER Apps)
endif()
