set(SOURCES 
            "Source/ProcessingOptions.cpp"
            "Source/ShaderCodeConditionNode.cpp"
            "Source/ShaderCodeCursor.cpp"
            "Source/ShaderCodeInliner.cpp"
            "Source/ShaderCodeNode.cpp"
            "Source/ShaderProcessor.cpp"
            "Source/WebGL2ShaderProcessor.cpp"
            "Source/ShaderDefineExpression.cpp"

            "Include/Babylon/ShaderProcessor/StringUtils.h"
            "Include/Babylon/ShaderProcessor/IShaderProcessor.h"
            "Include/Babylon/ShaderProcessor/ProcessingOptions.h"
            "Include/Babylon/ShaderProcessor/ShaderCodeConditionNode.h"
            "Include/Babylon/ShaderProcessor/ShaderCodeCursor.h"
            "Include/Babylon/ShaderProcessor/ShaderCodeInliner.h"
            "Include/Babylon/ShaderProcessor/ShaderCodeNode.h"
            "Include/Babylon/ShaderProcessor/ShaderProcessor.h"
            "Include/Babylon/ShaderProcessor/WebGL2ShaderProcessor.h"
            "Include/Babylon/ShaderProcessor/Expressions/ShaderDefineExpression.h"
            "Include/Babylon/ShaderProcessor/Expressions/Operators/ShaderDefineAndOperator.h"
            "Include/Babylon/ShaderProcessor/Expressions/Operators/ShaderDefineArithmeticOperator.h"
            "Include/Babylon/ShaderProcessor/Expressions/Operators/ShaderDefineIsDefinedOperator.h"
            "Include/Babylon/ShaderProcessor/Expressions/Operators/ShaderDefineOrOperator.h"
)
            

add_library(ShaderProcessor STATIC ${SOURCES})
target_include_directories(ShaderProcessor PUBLIC Include)
target_include_directories(ShaderProcessor PRIVATE Source)

target_link_to_dependencies(ShaderProcessor 
PRIVATE SPIRV
PRIVATE arcana
PRIVATE bgfx
PRIVATE bimg
PRIVATE bx
PRIVATE glslang)

target_compile_features(ShaderProcessor PRIVATE cxx_std_20)

set_property(TARGET ShaderProcessor PROPERTY FOLDER Tools)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})

if(TARGET spirv-cross-hlsl)
    target_link_to_dependencies(ShaderProcessor
        PRIVATE spirv-cross-hlsl)
elseif(TARGET spirv-cross-msl)
    target_link_to_dependencies(ShaderProcessor
        PRIVATE spirv-cross-msl)
else()
    target_link_to_dependencies(ShaderProcessor
        PRIVATE spirv-cross-glsl)
endif()

if(WIN32)
    target_link_to_dependencies(ShaderProcessor
        PRIVATE "d3dcompiler.lib")
endif()

target_compile_definitions(ShaderProcessor
    PRIVATE NOMINMAX)

target_compile_definitions(ShaderProcessor
    PRIVATE $<UPPER_CASE:${GRAPHICS_API}>)
