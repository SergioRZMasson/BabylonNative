cmake_minimum_required(VERSION 3.24)
project(LiteMinecraftNoUiComparison LANGUAGES CXX)
option(LITE_NO_UI_COMPARISON "Build isolated experimental canonical no-UI native binding." OFF)
if(NOT LITE_NO_UI_COMPARISON)
    message(FATAL_ERROR "This experimental build requires -DLITE_NO_UI_COMPARISON=ON.")
endif()
if(NOT WIN32)
    message(FATAL_ERROR "The current experimental native host is Win32/D3D11.")
endif()
set(BBLITEC_SOURCE_DIR "" CACHE PATH "Existing built compiler checkout.")
set(LITE_COMMON_EMITTED_DIR "" CACHE PATH "Parent-produced canonical STANDARD no-UI emission.")
set(LITE_NATIVE_LIBRARY_DIR "" CACHE PATH "Current rewritten Core exact-pin Release build.")
set(LITE_BGFX_SOURCE_DIR "" CACHE PATH "Declared bgfx.cmake source.")
set(LITE_TINT_LIBRARY_DIR "" CACHE PATH "Declared Release Tint archive directory.")
find_path(LITE_JSON_INCLUDE_DIR nlohmann/json.hpp REQUIRED)
find_program(NODE_EXECUTABLE node REQUIRED)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
get_filename_component(LITE_REPOSITORY "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
execute_process(COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/Tools/prepare.mjs"
    "${LITE_COMMON_EMITTED_DIR}" "${CMAKE_CURRENT_BINARY_DIR}" COMMAND_ERROR_IS_FATAL ANY)
file(GLOB_RECURSE USER_CPP "${CMAKE_CURRENT_BINARY_DIR}/UserSources/*.cpp")
set_source_files_properties(${USER_CPP} PROPERTIES COMPILE_DEFINITIONS "main=MinecraftApplication")
set(BASE "${CMAKE_CURRENT_SOURCE_DIR}/../Source")
add_executable(LiteMinecraftNoUiNative ${USER_CPP}
    "${CMAKE_CURRENT_BINARY_DIR}/BindingData/MaterialMetadata.cpp"
    "Source/StandardBinding.h" "Source/StandardBinding.cpp" "Source/NoUiHost.cpp"
    "${BASE}/C99Client.cpp" "${BASE}/AudioTransport.cpp" "${BASE}/Platform.cpp"
    "../../../LitePlayground/Source/NativeAudio.cpp")
target_include_directories(LiteMinecraftNoUiNative BEFORE PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/Override" "Source" "${BASE}"
    "${LITE_REPOSITORY}/Core/LiteLayer/Include"
    "${LITE_REPOSITORY}/Core/LiteShaderCompiler/Include")
target_include_directories(LiteMinecraftNoUiNative SYSTEM PRIVATE
    "${BBLITEC_SOURCE_DIR}/native/include" "${LITE_BGFX_SOURCE_DIR}/bgfx/include"
    "${LITE_JSON_INCLUDE_DIR}")
target_compile_definitions(LiteMinecraftNoUiNative PRIVATE
    LITE_MINECRAFT_NO_UI=1
    LITE_MINECRAFT_ASSETS="${LITE_COMMON_EMITTED_DIR}/Assets"
    LITE_MINECRAFT_ORIGINAL_ASSETS="${LITE_REPOSITORY}/Apps/LiteTests/LitePlayground/Assets/minecraft/voxelpack")
target_compile_options(LiteMinecraftNoUiNative PRIVATE /W4 /WX /EHsc /bigobj /Zc:__cplusplus)
foreach(LIBRARY IN ITEMS LiteLayer LiteShaderCompiler LiteShaderCompilerBackend LiteSpirvCross)
    if(LIBRARY STREQUAL "LiteLayer")
        set(LIBRARY_FILE "${LITE_NATIVE_LIBRARY_DIR}/Core/LiteLayer/${LIBRARY}.lib")
    else()
        set(LIBRARY_FILE "${LITE_NATIVE_LIBRARY_DIR}/Core/LiteShaderCompiler/${LIBRARY}.lib")
    endif()
    if(NOT EXISTS "${LIBRARY_FILE}")
        message(FATAL_ERROR "Missing already-built native archive: ${LIBRARY_FILE}")
    endif()
    list(APPEND NATIVE_LIBRARIES "${LIBRARY_FILE}")
endforeach()
foreach(LIBRARY IN ITEMS bgfx bimg bx)
    list(APPEND NATIVE_LIBRARIES
        "${LITE_NATIVE_LIBRARY_DIR}/_deps/bgfx.cmake-build/cmake/${LIBRARY}/${LIBRARY}.lib")
endforeach()
file(GLOB_RECURSE TINT_LIBRARIES "${LITE_TINT_LIBRARY_DIR}/*tint*.lib"
    "${LITE_TINT_LIBRARY_DIR}/*absl*.lib" "${LITE_TINT_LIBRARY_DIR}/*SPIRV-Tools*.lib")
target_link_libraries(LiteMinecraftNoUiNative PRIVATE ${NATIVE_LIBRARIES} ${TINT_LIBRARIES}
    d3dcompiler dxgi d3d11 user32 gdi32 ole32 windowscodecs comdlg32 psapi xaudio2)
set_property(TARGET LiteMinecraftNoUiNative PROPERTY FOLDER Apps/LiteTests/NoUiExperiment)
enable_testing()
add_test(NAME LiteNoUiCanonical180 COMMAND LiteMinecraftNoUiNative --frames=180 --headless
    --validate-bindings "--summary=${CMAKE_CURRENT_BINARY_DIR}/Qualification180/summary.json"
    "--capture-root=${CMAKE_CURRENT_BINARY_DIR}/Qualification180")
set_tests_properties(LiteNoUiCanonical180 PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)
