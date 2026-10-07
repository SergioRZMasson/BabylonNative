cmake_minimum_required(VERSION 3.24)
project(LiteMinecraftUserTerrain LANGUAGES CXX)
set(BBLITEC_SOURCE_DIR "" CACHE PATH "Existing built bblitec checkout.")
set(LITE_MINECRAFT_EMITTED_DIR "" CACHE PATH "FullMinecraft/Tools/generate.mjs Emitted directory.")
if(NOT EXISTS "${BBLITEC_SOURCE_DIR}/native/include/bblite/js_data.hpp" OR
    NOT EXISTS "${LITE_MINECRAFT_EMITTED_DIR}/sources/minecraft/worldgen.cpp")
    message(FATAL_ERROR "Set BBLITEC_SOURCE_DIR and LITE_MINECRAFT_EMITTED_DIR after full source-identical generation.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(USER_SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/UserSources")
file(MAKE_DIRECTORY "${USER_SOURCE_DIR}/sources/minecraft")
configure_file("Source/UserRuntimeData.h" "${USER_SOURCE_DIR}/sources/application.hpp" COPYONLY)
foreach(USER_MODULE IN ITEMS noise worldgen constants)
    configure_file("${LITE_MINECRAFT_EMITTED_DIR}/sources/minecraft/${USER_MODULE}.cpp"
        "${USER_SOURCE_DIR}/sources/minecraft/${USER_MODULE}.cpp" COPYONLY)
    list(APPEND USER_CPP "${USER_SOURCE_DIR}/sources/minecraft/${USER_MODULE}.cpp")
endforeach()
add_executable(LiteMinecraftUserTerrain "Source/WorldgenCheck.cpp" ${USER_CPP})
target_include_directories(LiteMinecraftUserTerrain PRIVATE "Source" "${BBLITEC_SOURCE_DIR}/native/include")
if(MSVC)
    target_compile_options(LiteMinecraftUserTerrain PRIVATE /W4 /WX /EHsc)
else()
    target_compile_options(LiteMinecraftUserTerrain PRIVATE -Wall -Wextra -Werror)
endif()
enable_testing()
add_test(NAME LiteMinecraftUserTerrain169 COMMAND LiteMinecraftUserTerrain)

option(LITE_MINECRAFT_BUILD_GAME "Build the complete C99-backed native application." OFF)
if(LITE_MINECRAFT_BUILD_GAME)
    if(NOT WIN32)
        message(FATAL_ERROR "Full native Minecraft currently uses the Win32 D3D11 host.")
    endif()
    set(LITE_NATIVE_LIBRARY_DIR "" CACHE PATH "Existing exact-pin Release native component build.")
    set(LITE_BGFX_SOURCE_DIR "" CACHE PATH "Declared exact-pin bgfx.cmake source.")
    set(LITE_TINT_LIBRARY_DIR "" CACHE PATH "Declared exact-pin Release Tint library tree.")
    find_path(LITE_JSON_INCLUDE_DIR nlohmann/json.hpp REQUIRED)
    execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${LITE_BGFX_SOURCE_DIR}"
        OUTPUT_VARIABLE LITE_BGFX_CMAKE_PIN OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${LITE_BGFX_SOURCE_DIR}/bgfx"
        OUTPUT_VARIABLE LITE_BGFX_PIN OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY)
    if(NOT LITE_BGFX_CMAKE_PIN STREQUAL "b53236f4d6dede3252d32b9c11921d0007d705d1" OR
        NOT LITE_BGFX_PIN STREQUAL "cb0c6d0c6133d123989aaae10cbd35fa139cb648")
        message(FATAL_ERROR "Native application requires the declared exact-pin bgfx sources.")
    endif()
    get_filename_component(LITE_REPOSITORY "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
    foreach(LIBRARY IN ITEMS LiteLayer LiteShaderCompiler LiteShaderCompilerBackend LiteSpirvCross)
        if(LIBRARY STREQUAL "LiteLayer")
            set(LIBRARY_FILE "${LITE_NATIVE_LIBRARY_DIR}/Core/LiteLayer/${LIBRARY}.lib")
        else()
            set(LIBRARY_FILE "${LITE_NATIVE_LIBRARY_DIR}/Core/LiteShaderCompiler/${LIBRARY}.lib")
        endif()
        if(NOT EXISTS "${LIBRARY_FILE}")
            message(FATAL_ERROR "Build exact-pin native components first: ${LIBRARY_FILE}")
        endif()
        list(APPEND NATIVE_LIBRARIES "${LIBRARY_FILE}")
    endforeach()
    foreach(LIBRARY IN ITEMS bgfx bimg bx)
        list(APPEND NATIVE_LIBRARIES
            "${LITE_NATIVE_LIBRARY_DIR}/_deps/bgfx.cmake-build/cmake/${LIBRARY}/${LIBRARY}.lib")
    endforeach()
    file(GLOB_RECURSE TINT_LIBRARIES "${LITE_TINT_LIBRARY_DIR}/*tint*.lib"
        "${LITE_TINT_LIBRARY_DIR}/*absl*.lib" "${LITE_TINT_LIBRARY_DIR}/*SPIRV-Tools*.lib")
    find_program(NODE_EXECUTABLE node REQUIRED)
    set(PROJECTED_DIR "${CMAKE_CURRENT_BINARY_DIR}/Projected")
    execute_process(COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/Tools/project-game.mjs"
        "${BBLITEC_SOURCE_DIR}" "${LITE_MINECRAFT_EMITTED_DIR}" "${PROJECTED_DIR}"
        COMMAND_ERROR_IS_FATAL ANY)
    file(GLOB_RECURSE PROJECTED_CPP "${PROJECTED_DIR}/*.cpp")
    add_executable(LiteMinecraftNative ${PROJECTED_CPP}
        "Source/C99Client.h" "Source/C99Client.cpp" "Source/Platform.h" "Source/Platform.cpp"
        "Source/AudioTransport.cpp" "Source/NativeHost.cpp" "Source/AdapterCheck.cpp"
        "../../LitePlayground/Source/NativeAudio.cpp")
    target_include_directories(LiteMinecraftNative PRIVATE "Source"
        "${LITE_REPOSITORY}/Core/LiteLayer/Include"
        "${LITE_REPOSITORY}/Core/LiteShaderCompiler/Include")
    target_include_directories(LiteMinecraftNative SYSTEM PRIVATE
        "${BBLITEC_SOURCE_DIR}/native/include" "${LITE_BGFX_SOURCE_DIR}/bgfx/include"
        "${LITE_JSON_INCLUDE_DIR}")
    target_compile_definitions(LiteMinecraftNative PRIVATE
        LITE_MINECRAFT_ASSETS="${LITE_MINECRAFT_EMITTED_DIR}/Assets"
        LITE_MINECRAFT_ORIGINAL_ASSETS="${LITE_REPOSITORY}/Apps/LiteTests/LitePlayground/Assets/minecraft/voxelpack")
    target_compile_options(LiteMinecraftNative PRIVATE /W4 /WX /EHsc /bigobj /Zc:__cplusplus)
    target_link_libraries(LiteMinecraftNative PRIVATE ${NATIVE_LIBRARIES} ${TINT_LIBRARIES}
        d3dcompiler dxgi d3d11 user32 gdi32 ole32 windowscodecs comdlg32 psapi xaudio2)
    set_property(TARGET LiteMinecraftNative PROPERTY FOLDER Apps/LiteTests)
    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Source" PREFIX Host
        FILES "Source/C99Client.h" "Source/C99Client.cpp" "Source/Platform.h"
        "Source/Platform.cpp" "Source/AudioTransport.cpp" "Source/NativeHost.cpp"
        "Source/AdapterCheck.cpp")
    add_test(NAME LiteMinecraftC99AdapterContract COMMAND LiteMinecraftNative --adapter-contract)
    add_test(NAME LiteMinecraftNativeFull180 COMMAND LiteMinecraftNative --frames=180 --headless
        "--capture-root=${CMAKE_CURRENT_BINARY_DIR}/Captures/Full180"
        "--summary=${CMAKE_CURRENT_BINARY_DIR}/Captures/Full180/summary.json")
    add_test(NAME LiteMinecraftNativeReplay240 COMMAND LiteMinecraftNative --frames=240 --headless
        --replay "--capture-root=${CMAKE_CURRENT_BINARY_DIR}/Captures/Replay240"
        "--summary=${CMAKE_CURRENT_BINARY_DIR}/Captures/Replay240/summary.json")
    add_test(NAME LiteMinecraftNativeSaveLoad240 COMMAND LiteMinecraftNative --frames=240 --headless
        --test-save-load "--capture-root=${CMAKE_CURRENT_BINARY_DIR}/Captures/SaveLoad240"
        "--summary=${CMAKE_CURRENT_BINARY_DIR}/Captures/SaveLoad240/summary.json")
    add_test(NAME LiteMinecraftNativeBenchmarkControl COMMAND LiteMinecraftNative
        --warmup=180 --measure=1200
        "--benchmark-output=${CMAKE_CURRENT_BINARY_DIR}/Benchmark/control.json")
    set_tests_properties(LiteMinecraftC99AdapterContract LiteMinecraftNativeFull180
        LiteMinecraftNativeReplay240 LiteMinecraftNativeSaveLoad240
        LiteMinecraftNativeBenchmarkControl PROPERTIES
        RUN_SERIAL TRUE TIMEOUT 300)
endif()
