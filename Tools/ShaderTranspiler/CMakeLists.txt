set(BABYLON_SCRIPTS
    "node_modules/babylonjs-loaders/babylonjs.loaders.js"
    "node_modules/babylonjs-loaders/babylonjs.loaders.js.map"
    "node_modules/babylonjs/babylon.max.js"
    "node_modules/babylonjs/babylon.max.js.map"
    "node_modules/babylonjs-materials/babylonjs.materials.js"
    "node_modules/babylonjs-materials/babylonjs.materials.js.map"
    "node_modules/babylonjs-gui/babylon.gui.js"
    "node_modules/babylonjs-gui/babylon.gui.js.map")

set(SCRIPTS
    "Scripts/experience.js")


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
        

add_executable(ShaderTranspiler ${SOURCES} ${SCRIPTS} ${BABYLON_SCRIPTS})
target_include_directories(ShaderTranspiler PRIVATE Include)
target_include_directories(ShaderTranspiler PRIVATE Source)

target_link_to_dependencies(ShaderTranspiler 
    PRIVATE SPIRV
    PRIVATE arcana
    PRIVATE bgfx
    PRIVATE bimg
    PRIVATE bx
    PRIVATE glslang
    PRIVATE AppRuntime
    PRIVATE NativeCapture
    PRIVATE ChromeDevTools
    PRIVATE NativeEngine
    PRIVATE NativeInput
    PRIVATE NativeOptimizations
    PRIVATE Console
    PRIVATE Window
    PRIVATE ScriptLoader
    PRIVATE XMLHttpRequest
    PRIVATE Canvas
)

target_compile_features(ShaderTranspiler PRIVATE cxx_std_17)

if(WINDOWS_STORE)
    set_property(SOURCE ${SCRIPTS} ${BABYLON_SCRIPTS} PROPERTY VS_DEPLOYMENT_CONTENT 1)
    set_property(SOURCE ${SCRIPTS} ${BABYLON_SCRIPTS} PROPERTY VS_DEPLOYMENT_LOCATION "Scripts")
else()
    foreach(SCRIPT ${SCRIPTS} ${BABYLON_SCRIPTS})
        get_filename_component(SCRIPT_NAME "${SCRIPT}" NAME)
        add_custom_command(
            OUTPUT "${CMAKE_CFG_INTDIR}/Scripts/${SCRIPT_NAME}"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/${SCRIPT}" "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/Scripts/${SCRIPT_NAME}"
            COMMENT "Copying ${SCRIPT_NAME}"
            MAIN_DEPENDENCY "${CMAKE_CURRENT_SOURCE_DIR}/${SCRIPT}")
    endforeach()
endif()

set_property(TARGET ShaderTranspiler PROPERTY FOLDER Tools)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/node_modules" PREFIX Scripts FILES ${BABYLON_SCRIPTS})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SCRIPTS})

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
