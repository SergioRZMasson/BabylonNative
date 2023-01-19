set(SOURCES "Source/main.cpp" 
            "Source/ResourceLimits.cpp"
            "Source/ResourceLimits.cpp"
            "Source/ResourceLimits.h"
            "Source/ShaderCompiler.h"
            "Source/ShaderCompilerCommon.h"
            "Source/ShaderCompilerCommon.cpp"
            "Source/ShaderCompilerTraversers.cpp"
            "Source/ShaderCompilerTraversers.h"
            "Source/ShaderCompiler${GRAPHICS_API}.cpp")

if(WIN32)
    set(SOURCES ${SOURCES} "Source/ShaderCompilerD3D.h")
endif()
        

add_executable(ShaderTranspiler ${SOURCES})
target_include_directories(ShaderTranspiler PRIVATE Include)
target_include_directories(ShaderTranspiler PRIVATE Source)

target_link_to_dependencies(ShaderTranspiler 
PRIVATE SPIRV
PRIVATE arcana
PRIVATE bgfx
PRIVATE bimg
PRIVATE bx
PRIVATE glslang)

target_compile_features(ShaderTranspiler PRIVATE cxx_std_17)

set_property(TARGET ShaderTranspiler PROPERTY FOLDER Tools)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})

if(TARGET spirv-cross-hlsl)
    target_link_to_dependencies(ShaderTranspiler
        PRIVATE spirv-cross-hlsl)
elseif(TARGET spirv-cross-msl)
    target_link_to_dependencies(ShaderTranspiler
        PRIVATE spirv-cross-msl)
else()
    target_link_to_dependencies(ShaderTranspiler
        PRIVATE spirv-cross-glsl)
endif()

if(WIN32)
    target_link_to_dependencies(ShaderTranspiler
        PRIVATE "d3dcompiler.lib")
endif()

target_compile_definitions(ShaderTranspiler
    PRIVATE NOMINMAX)

target_compile_definitions(ShaderTranspiler
    PRIVATE $<UPPER_CASE:${GRAPHICS_API}>)
