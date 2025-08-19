FILE(GLOB ASSETS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "assets/*")

set(INTEGRATION_APP_SOURCES 
    "Source/main.cpp" 
    "Source/BabylonRenderer.h"
    "Source/BabylonRenderer.cpp"
    "Source/BabylonMath.h"
    "Source/stb_image_write.h"
    "Source/GraphicsDebug.h"
    "Source/PlatformUtils.h"
    "Source/BabylonRendererImpl_Shared.h")

if(GRAPHICS_API STREQUAL D3D11)
    set(INTEGRATION_APP_SOURCES ${INTEGRATION_APP_SOURCES}
        "Source/Platforms/D3D11/BabylonRendererImpl.cpp"
        "Source/Platforms/D3D11/GraphicsDebug.cpp"
        "Source/Platforms/D3D11/PlatformUtils.cpp"
        "Include/Platforms/D3D11/RenderTypes.h"
        "Include/Platforms/D3D11/BabylonRendererImpl.h")
elseif(GRAPHICS_API STREQUAL Metal)
    set(INTEGRATION_APP_SOURCES ${INTEGRATION_APP_SOURCES}
        "Source/Platforms/Metal/BabylonRendererImpl.mm"
        "Source/Platforms/Metal/GraphicsDebug.mm"
        "Source/Platforms/Metal/PlatformUtils.mm"
        "Include/Platforms/Metal/RenderTypes.mm"
        "Include/Platforms/Metal/BabylonRendererImpl.cpp")
endif()

add_executable(IntegrationTestApp ${INTEGRATION_APP_SOURCES} ${ASSETS})

target_include_directories(IntegrationTestApp PRIVATE 
    "Source"
    "Include/Platforms/${GRAPHICS_API}")

target_link_libraries(IntegrationTestApp
    PRIVATE AppRuntime
    PRIVATE Console
    PRIVATE ExternalTexture
    PRIVATE GraphicsDevice
    PRIVATE NativeEngine
    PRIVATE ScriptLoader
    PRIVATE Window
    PRIVATE XMLHttpRequest)

foreach(Assets ${ASSETS})
    get_filename_component(ASSET_NAME "${asset}" NAME)
    add_custom_command(
        OUTPUT "${CMAKE_CFG_INTDIR}/Assets/${ASSET_NAME}"
        COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/${asset}" "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/Assets/${ASSET_NAME}"
        COMMENT "Copying ${ASSET_NAME}"
        MAIN_DEPENDENCY "${CMAKE_CURRENT_SOURCE_DIR}/${asset}")
endforeach()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${ASSETS})

target_compile_definitions(IntegrationTestApp PRIVATE NOMINMAX)