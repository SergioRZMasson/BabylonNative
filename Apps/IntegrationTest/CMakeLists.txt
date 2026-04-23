set(SCRIPTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Scripts")
set(BUNDLE_OUTPUT "${SCRIPTS_DIR}/dist/BabylonInterop.bundle.js")

# Run npm install at configure time (downloads @babylonjs packages).
npm(install WORKING_DIRECTORY "${SCRIPTS_DIR}")

set(SOURCES
    "Source/App.cpp"
    "Source/BabylonRenderer.h"
    "Source/BabylonRenderer.cpp"
    "Source/OrbitCamera.h"
    "Source/RenderDocCapture.h"
    "Source/MathTypes.h")

option(INTEGRATION_TEST_RENDERDOC "Enable RenderDoc frame capture support (requires RenderDoc installation)" OFF)

add_executable(IntegrationTest WIN32 ${SOURCES})

target_include_directories(IntegrationTest PRIVATE "Source")

target_link_libraries(IntegrationTest
    PRIVATE AppRuntime
    PRIVATE Console
    PRIVATE ExternalTexture
    PRIVATE GraphicsDevice
    PRIVATE NativeEngine
    PRIVATE ScriptLoader
    PRIVATE Window
    PRIVATE XMLHttpRequest)

target_compile_definitions(IntegrationTest PRIVATE NOMINMAX UNICODE _UNICODE)

if(INTEGRATION_TEST_RENDERDOC)
    find_path(RENDERDOC_INCLUDE_DIR renderdoc_app.h
        HINTS
            "$ENV{ProgramFiles}/RenderDoc"
            "$ENV{ProgramW6432}/RenderDoc"
            "C:/Program Files/RenderDoc")
    if(RENDERDOC_INCLUDE_DIR)
        message(STATUS "RenderDoc found: ${RENDERDOC_INCLUDE_DIR}")
        target_include_directories(IntegrationTest PRIVATE "${RENDERDOC_INCLUDE_DIR}")
        target_compile_definitions(IntegrationTest PRIVATE INTEGRATION_TEST_RENDERDOC)
    else()
        message(WARNING "INTEGRATION_TEST_RENDERDOC is ON but renderdoc_app.h was not found. "
                        "Install RenderDoc or set RENDERDOC_INCLUDE_DIR.")
    endif()
endif()

# Copy V8 runtime DLLs to the output directory (same approach as Playground).
# See https://gitlab.kitware.com/cmake/cmake/-/issues/23543
add_custom_command(TARGET IntegrationTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E $<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:IntegrationTest>>,copy,true> $<TARGET_RUNTIME_DLLS:IntegrationTest> $<TARGET_FILE_DIR:IntegrationTest> COMMAND_EXPAND_LISTS)

# Build the JS bundle at compile time via webpack.
# Glob the TS sources so the bundle rebuilds when they change.
file(GLOB_RECURSE TS_SOURCES "${SCRIPTS_DIR}/src/*.ts")
add_custom_command(
    OUTPUT "${BUNDLE_OUTPUT}"
    COMMAND npx webpack --mode production
    WORKING_DIRECTORY "${SCRIPTS_DIR}"
    DEPENDS ${TS_SOURCES} "${SCRIPTS_DIR}/webpack.config.js" "${SCRIPTS_DIR}/tsconfig.json"
    COMMENT "Building BabylonInterop.bundle.js")
add_custom_target(IntegrationTestBundle DEPENDS "${BUNDLE_OUTPUT}")
add_dependencies(IntegrationTest IntegrationTestBundle)

# Copy JS bundle to output directory
add_custom_command(TARGET IntegrationTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:IntegrationTest>/Scripts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${BUNDLE_OUTPUT}"
        "$<TARGET_FILE_DIR:IntegrationTest>/Scripts/BabylonInterop.bundle.js"
    COMMENT "Copying BabylonInterop.bundle.js")

# Copy assets to output directory
file(GLOB ASSET_FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/*")
foreach(ASSET ${ASSET_FILES})
    get_filename_component(ASSET_NAME "${ASSET}" NAME)
    add_custom_command(TARGET IntegrationTest POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:IntegrationTest>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ASSET}"
            "$<TARGET_FILE_DIR:IntegrationTest>/assets/${ASSET_NAME}"
        COMMENT "Copying ${ASSET_NAME}")
endforeach()
