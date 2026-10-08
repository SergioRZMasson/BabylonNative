cmake_minimum_required(VERSION 3.24)
project(LiteMinecraftRmlUiExperiment LANGUAGES C CXX)
option(LITE_MINECRAFT_RMLUI "Build original STANDARD C++ Minecraft with retained C99 UI." OFF)
if(NOT LITE_MINECRAFT_RMLUI OR NOT WIN32)
    message(FATAL_ERROR "Opt-in Win32 host requires LITE_MINECRAFT_RMLUI=ON.")
endif()
set(BBLITEC_SOURCE_DIR "" CACHE PATH "Existing standard compiler checkout for user headers only.")
set(LITE_COMMON_EMITTED_DIR "" CACHE PATH "Immutable canonical STANDARD full-UI emission.")
set(LITE_UI_COMPILER_LIBRARY_DIR "" CACHE PATH "Qualified shader compiler archive root.")
set(LITE_UI_TINT_LIBRARY_DIR "" CACHE PATH "Exact-pin Release Tint archive directory.")
find_path(LITE_JSON_INCLUDE_DIR nlohmann/json.hpp REQUIRED)
find_program(NODE_EXECUTABLE node REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(LITE_REPOSITORY "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
add_compile_definitions("$<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=0>")
include(FetchContent)
function(warnings_as_errors target)
    target_compile_options(${target} PRIVATE /W4 /WX)
endfunction()
set(BGFX_BUILD_EXAMPLES OFF)
set(BGFX_BUILD_TOOLS OFF)
set(BGFX_INSTALL OFF)
set(BGFX_CONFIG_MULTITHREADED ON)
FetchContent_Declare(LiteBgfx
    GIT_REPOSITORY https://github.com/BabylonJS/bgfx.cmake.git
    GIT_TAG b53236f4d6dede3252d32b9c11921d0007d705d1 EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(LiteBgfx)
target_compile_definitions(bgfx PRIVATE BGFX_CONFIG_RENDERER_WEBGPU=0 BGFX_CONFIG_C99_API=0
    BGFX_CONFIG_RENDERER_DIRECT3D11=1)
target_compile_definitions(bx PRIVATE _CRT_SECURE_NO_WARNINGS)
function(FetchContent_MakeAvailable_With_Message dependency)
    FetchContent_MakeAvailable(${dependency})
endfunction()
FetchContent_Declare(RmlUi
    GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
    GIT_TAG b7b4a0688262832eacf3b9abb41f8bbe73868af8 EXCLUDE_FROM_ALL)
FetchContent_Declare(LiteFreetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG 42608f77f20749dd6ddc9e0536788eaad70ea4b5 EXCLUDE_FROM_ALL)
set(BABYLON_LITE_ENABLE_UI ON CACHE BOOL "" FORCE)
include("${LITE_REPOSITORY}/Dependencies/RmlUi.cmake")
set(BABYLON_LITE_ENABLE_STYLE_CHECKS ON CACHE BOOL "" FORCE)
set(BABYLON_NATIVE_BUILD_LITE_UI_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${LITE_REPOSITORY}/Core/LiteLayer" LiteLayer)
add_library(MinecraftShaderService INTERFACE)
file(GLOB_RECURSE COMPILER_LIBRARIES
    "${LITE_UI_COMPILER_LIBRARY_DIR}/LiteShaderCompiler.lib"
    "${LITE_UI_COMPILER_LIBRARY_DIR}/LiteShaderCompilerBackend.lib"
    "${LITE_UI_COMPILER_LIBRARY_DIR}/LiteSpirvCross.lib")
file(GLOB_RECURSE TINT_LIBRARIES "${LITE_UI_TINT_LIBRARY_DIR}/*tint*.lib"
    "${LITE_UI_TINT_LIBRARY_DIR}/*absl*.lib"
    "${LITE_UI_TINT_LIBRARY_DIR}/*SPIRV-Tools*.lib")
if(NOT COMPILER_LIBRARIES OR NOT TINT_LIBRARIES)
    message(FATAL_ERROR "Missing actual qualified compiler or exact-pin Tint libraries.")
endif()
target_link_libraries(MinecraftShaderService INTERFACE ${COMPILER_LIBRARIES} ${TINT_LIBRARIES}
    d3dcompiler)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "Tools/prepare.mjs" "Tools/prepare-platform.py" "${LITE_COMMON_EMITTED_DIR}/manifest.json"
    "${LITE_REPOSITORY}/Core/LiteLayer/Include/babylon_lite.h")
file(READ "${LITE_REPOSITORY}/Core/LiteLayer/Include/babylon_lite.h" LITE_UI_HEADER)
if(LITE_UI_HEADER MATCHES "bl_setUiWhiteDifference\\(")
    set_source_files_properties("Source/UiBridge.cpp" PROPERTIES
        COMPILE_DEFINITIONS "LITE_UI_HAS_WHITE_DIFFERENCE=1")
endif()
execute_process(COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/Tools/prepare.mjs"
    "${LITE_COMMON_EMITTED_DIR}" "${CMAKE_CURRENT_BINARY_DIR}" COMMAND_ERROR_IS_FATAL ANY)
set(BASE "${CMAKE_CURRENT_SOURCE_DIR}/../Source")
execute_process(COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/Tools/prepare-platform.py"
    "${BASE}/Platform.cpp" "${CMAKE_CURRENT_BINARY_DIR}/BindingData" COMMAND_ERROR_IS_FATAL ANY)
file(GLOB_RECURSE USER_CPP "${CMAKE_CURRENT_BINARY_DIR}/UserSources/*.cpp")
set_source_files_properties(${USER_CPP} PROPERTIES COMPILE_DEFINITIONS "main=MinecraftApplication")
add_executable(LiteMinecraftRmlUiNative ${USER_CPP}
    "${CMAKE_CURRENT_BINARY_DIR}/BindingData/MaterialMetadata.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/BindingData/PlatformTransport.cpp"
    "Source/StandardBinding.cpp" "Source/NativeHost.cpp" "Source/UiBridge.cpp" "Source/BoundaryTests.cpp"
    "Source/CssTransition.cpp"
    "${BASE}/C99Client.cpp" "${BASE}/AudioTransport.cpp"
    "../../../LitePlayground/Source/NativeAudio.cpp")
target_include_directories(LiteMinecraftRmlUiNative BEFORE PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/Override" "Source" "${BASE}"
    "${LITE_REPOSITORY}/Core/LiteShaderCompiler/Include")
target_include_directories(LiteMinecraftRmlUiNative SYSTEM PRIVATE
    "${BBLITEC_SOURCE_DIR}/native/include" "${LITE_JSON_INCLUDE_DIR}")
target_compile_definitions(LiteMinecraftRmlUiNative PRIVATE
    LITE_MINECRAFT_ASSETS="${LITE_COMMON_EMITTED_DIR}/assets"
    LITE_MINECRAFT_ORIGINAL_ASSETS="${LITE_REPOSITORY}/Apps/LiteTests/LitePlayground/Assets/minecraft/voxelpack")
target_compile_options(LiteMinecraftRmlUiNative PRIVATE /W4 /WX /EHsc /bigobj /Zc:__cplusplus)
target_link_libraries(LiteMinecraftRmlUiNative PRIVATE LiteLayer bgfx MinecraftShaderService
    d3dcompiler dxgi d3d11 user32 ole32 windowscodecs comdlg32 psapi xaudio2)
target_link_options(LiteMinecraftRmlUiNative PRIVATE
    "/MAP:${CMAKE_CURRENT_BINARY_DIR}/LiteMinecraftRmlUiNative.map")
enable_testing()
add_library(LiteMinecraftUiHeaderC99 OBJECT "Source/ContractC99.c")
target_include_directories(LiteMinecraftUiHeaderC99 PRIVATE "${LITE_REPOSITORY}/Core/LiteLayer/Include")
set_target_properties(LiteMinecraftUiHeaderC99 PROPERTIES C_STANDARD 99 C_EXTENSIONS OFF)
warnings_as_errors(LiteMinecraftUiHeaderC99)
add_executable(LiteMinecraftUiTranslationTests "Source/TranslationTests.cpp" "Source/UiBridge.cpp"
    "Source/CssTransition.cpp")
target_include_directories(LiteMinecraftUiTranslationTests PRIVATE "Source" "${BASE}")
target_include_directories(LiteMinecraftUiTranslationTests SYSTEM PRIVATE
    "${BBLITEC_SOURCE_DIR}/native/include" "${LITE_JSON_INCLUDE_DIR}")
target_compile_definitions(LiteMinecraftUiTranslationTests PRIVATE
    LITE_MINECRAFT_ORIGINAL_ASSETS="${LITE_REPOSITORY}/Apps/LiteTests/LitePlayground/Assets/minecraft/voxelpack")
target_compile_options(LiteMinecraftUiTranslationTests PRIVATE /W4 /WX /EHsc /Zc:__cplusplus)
target_link_libraries(LiteMinecraftUiTranslationTests PRIVATE LiteLayer user32 ole32 windowscodecs)
add_test(NAME LiteMinecraftUiTranslationTests COMMAND LiteMinecraftUiTranslationTests)
add_test(NAME LiteMinecraftUiToolTests COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/Tools/test_tools.py")
add_test(NAME LiteMinecraftUiBoundaryTests COMMAND LiteMinecraftRmlUiNative
    --headless --ui-boundary-test
    "--capture-root=${CMAKE_CURRENT_BINARY_DIR}/UiBoundary")
set_tests_properties(LiteMinecraftUiBoundaryTests PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)
foreach(CASE IN ITEMS idle180 idle1380 replay240 save-load240 toast-expiry500 resize180 underwater180)
    add_test(NAME "LiteMinecraftRmlUi-${CASE}" COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/Tools/run-case.py" "${CMAKE_CURRENT_BINARY_DIR}" "${CASE}")
    set_tests_properties("LiteMinecraftRmlUi-${CASE}" PROPERTIES RUN_SERIAL TRUE TIMEOUT 360)
endforeach()
