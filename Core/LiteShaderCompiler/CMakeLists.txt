if(NOT WIN32 OR NOT GRAPHICS_API STREQUAL "D3D11")
    message(FATAL_ERROR "LiteShaderCompiler currently implements the D3D11 runtime backend only.")
endif()

file(STRINGS "${BGFX_DIR}/tools/shaderc/shaderc.cpp" LITE_BGFX_SHADER_VERSION_LINE
    REGEX "^#define BGFX_SHADER_BIN_VERSION ")
if(NOT LITE_BGFX_SHADER_VERSION_LINE MATCHES "BGFX_SHADER_BIN_VERSION 12$")
    message(FATAL_ERROR "LiteShaderCompiler requires the declared bgfx shader container v12 dependency, not an older reused cache.")
endif()

# bgfx's trimmed Tint snapshot lacks the WGSL reader. Use the complete public
# compiler at the same pin as the test application's independent Tint reference.
FetchContent_Declare(LiteTint
    GIT_REPOSITORY https://github.com/google/dawn.git
    GIT_TAG a21a4a1c7c497e6366947ccaefbab768d16f32a8
    EXCLUDE_FROM_ALL)

set(BABYLON_LITE_TINT_LIBRARY_DIR "" CACHE PATH "Optional Release library directory for the pinned Tint compiler.")
if(BABYLON_LITE_TINT_LIBRARY_DIR)
    FetchContent_GetProperties(LiteTint)
    if(NOT FETCHCONTENT_SOURCE_DIR_LITETINT)
        message(FATAL_ERROR "Prebuilt Tint requires FETCHCONTENT_SOURCE_DIR_LITETINT at the declared source pin.")
    endif()
    set(LiteTint_SOURCE_DIR "${FETCHCONTENT_SOURCE_DIR_LITETINT}")
    execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${LiteTint_SOURCE_DIR}"
        OUTPUT_VARIABLE LITE_TINT_SOURCE_PIN OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    if(NOT LITE_TINT_SOURCE_PIN STREQUAL "a21a4a1c7c497e6366947ccaefbab768d16f32a8")
        message(FATAL_ERROR "Prebuilt Tint source must match the declared compiler pin.")
    endif()
    file(GLOB_RECURSE LITE_TINT_LIBRARIES
        "${BABYLON_LITE_TINT_LIBRARY_DIR}/*tint*.lib"
        "${BABYLON_LITE_TINT_LIBRARY_DIR}/*absl*.lib"
        "${BABYLON_LITE_TINT_LIBRARY_DIR}/*SPIRV-Tools*.lib")
    if(NOT LITE_TINT_LIBRARIES)
        message(FATAL_ERROR "No pinned Tint libraries found at BABYLON_LITE_TINT_LIBRARY_DIR.")
    endif()
    add_library(LiteTintCompiler INTERFACE)
    target_include_directories(LiteTintCompiler SYSTEM INTERFACE "${LiteTint_SOURCE_DIR}")
    target_compile_definitions(LiteTintCompiler INTERFACE
        TINT_BUILD_WGSL_READER=1 TINT_BUILD_SPV_WRITER=1 TINT_BUILD_IR_BINARY=0
        TINT_BUILD_WGSL_WRITER=0 TINT_BUILD_HLSL_WRITER=0 TINT_BUILD_MSL_WRITER=0
        TINT_BUILD_GLSL_WRITER=0 TINT_BUILD_SPV_READER=0)
    target_link_libraries(LiteTintCompiler INTERFACE ${LITE_TINT_LIBRARIES})
else()
    foreach(LITE_TINT_DISABLED_OPTION IN ITEMS
        DAWN_ENABLE_D3D11 DAWN_ENABLE_D3D12 DAWN_ENABLE_VULKAN DAWN_ENABLE_METAL
        DAWN_ENABLE_NULL DAWN_ENABLE_DESKTOP_GL DAWN_ENABLE_OPENGLES DAWN_USE_WINDOWS_UI
        DAWN_USE_GLFW DAWN_BUILD_SAMPLES DAWN_BUILD_TESTS DAWN_BUILD_BENCHMARKS
        DAWN_BUILD_PROTOBUF DAWN_BUILD_MONOLITHIC_LIBRARY DAWN_SUPPORTS_CXX_MODULES
        TINT_BUILD_TESTS TINT_BUILD_BENCHMARKS TINT_BUILD_CMD_TOOLS TINT_BUILD_IR_BINARY
        TINT_BUILD_WGSL_WRITER TINT_BUILD_HLSL_WRITER TINT_BUILD_MSL_WRITER
        TINT_BUILD_GLSL_WRITER TINT_BUILD_GLSL_VALIDATOR TINT_BUILD_SPV_READER)
        set(${LITE_TINT_DISABLED_OPTION} OFF CACHE BOOL "" FORCE)
    endforeach()
    set(TINT_BUILD_WGSL_READER ON CACHE BOOL "" FORCE)
    set(TINT_BUILD_SPV_WRITER ON CACHE BOOL "" FORCE)
    set(DAWN_FETCH_DEPENDENCIES ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(LiteTint)
    add_library(LiteTintCompiler INTERFACE)
    target_link_libraries(LiteTintCompiler INTERFACE
        tint_api tint_lang_wgsl_reader tint_lang_wgsl_inspector
        tint_lang_spirv_writer tint_lang_spirv_writer_common)
endif()

set(SOURCES
    "Source/CompilerBackend.h"
    "Source/CompilerBackend.cpp")

# WGSL lowers aggregate entry-point IO that NativeEngine's trimmed GLSL-only
# SPIRV-Cross does not need. Keep the complete compiler private to this service.
get_target_property(LITE_SPIRV_CROSS_SOURCE_DIR spirv-cross-core SOURCE_DIR)
add_library(LiteSpirvCross STATIC
    "${LITE_SPIRV_CROSS_SOURCE_DIR}/spirv_cross.cpp"
    "${LITE_SPIRV_CROSS_SOURCE_DIR}/spirv_parser.cpp"
    "${LITE_SPIRV_CROSS_SOURCE_DIR}/spirv_cross_parsed_ir.cpp"
    "${LITE_SPIRV_CROSS_SOURCE_DIR}/spirv_cfg.cpp"
    "${LITE_SPIRV_CROSS_SOURCE_DIR}/spirv_hlsl.cpp")
target_include_directories(LiteSpirvCross SYSTEM PUBLIC "${LITE_SPIRV_CROSS_SOURCE_DIR}")
target_compile_definitions(LiteSpirvCross PUBLIC spirv_cross=lite_spirv_cross)
set_property(TARGET LiteSpirvCross PROPERTY FOLDER Dependencies/LiteShaderCompiler)

add_library(LiteShaderCompilerBackend STATIC ${SOURCES})
warnings_as_errors(LiteShaderCompilerBackend)
target_link_libraries(LiteShaderCompilerBackend PRIVATE LiteTintCompiler LiteSpirvCross bgfx bx d3dcompiler)
if(MSVC)
    target_compile_options(LiteShaderCompilerBackend PRIVATE /wd4127 /wd4244 /wd4267 /wd4324 /wd4458 /wd4459)
endif()
set_property(TARGET LiteShaderCompilerBackend PROPERTY FOLDER Core)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})

set(SERVICE_SOURCES
    "Include/babylon_lite_shader_compiler.h"
    "Source/ShaderCompilerService.cpp")
add_library(LiteShaderCompiler STATIC ${SERVICE_SOURCES})
warnings_as_errors(LiteShaderCompiler)
target_include_directories(LiteShaderCompiler PUBLIC "Include" "${CMAKE_CURRENT_SOURCE_DIR}/../LiteLayer/Include")
target_link_libraries(LiteShaderCompiler PRIVATE LiteShaderCompilerBackend bgfx)
set_property(TARGET LiteShaderCompiler PROPERTY FOLDER Core)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SERVICE_SOURCES})
