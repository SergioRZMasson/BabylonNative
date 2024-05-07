if(APPLE)
    set(PLATFORM Apple)
else()
    set(PLATFORM ${BABYLON_NATIVE_PLATFORM})
endif()

set(SOURCES
    "Include/Babylon/Plugins/TestUtils.h"
    "Source/TestUtils.h"
    "Source/TestUtils.cpp"
    "Source/TestUtilsImplData.h"
    "Source/${PLATFORM}/TestUtilsImpl.${BABYLON_NATIVE_PLATFORM_IMPL_EXT}"
    "Source/Graphics/TestUtilsImpl_${GRAPHICS_API}.cpp")

add_library(TestUtils ${SOURCES})
warnings_as_errors(TestUtils)

target_include_directories(TestUtils PUBLIC
    "Include")

target_link_libraries(TestUtils
    PUBLIC napi
    PRIVATE JsRuntimeInternal
    PRIVATE GraphicsDevice
    PRIVATE GraphicsDeviceContext)

target_compile_definitions(TestUtils PRIVATE GRAPHICS_API=${GRAPHICS_API})

set_property(TARGET TestUtils PROPERTY FOLDER Plugins)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})

if(BABYLON_RENDER_DOC_PATH)
    target_include_directories(TestUtils PRIVATE ${BABYLON_RENDER_DOC_PATH})
    target_compile_definitions(TestUtils PRIVATE BABYLON_TEST_UTILS_RENDER_DOC_DEBUG=1)
    target_compile_definitions(TestUtils PRIVATE BABYLON_TEST_UTILS_RENDER_DOC_LIBRARY_PATH=L"${BABYLON_RENDER_DOC_PATH}\\\\renderdoc.dll")
endif()

