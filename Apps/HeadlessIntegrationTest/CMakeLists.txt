# HeadlessIntegrationTest uses the UMD babylon.max.js bundle (same as
# HeadlessScreenshotApp) for a proven rendering path, plus a small
# index.js harness that exposes startup() and loadAndRenderModelAsync().
set(INTEGRATION_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../IntegrationTest")

set(BABYLON_SCRIPTS
    "../node_modules/babylonjs/babylon.max.js"
    "../node_modules/babylonjs-loaders/babylonjs.loaders.js")

set(LOCAL_SCRIPTS
    "Scripts/index.js")

set(SOURCES
    "Source/App.cpp")

add_executable(HeadlessIntegrationTest ${SOURCES})

target_compile_definitions(HeadlessIntegrationTest
    PRIVATE NOMINMAX
    PRIVATE UNICODE
    PRIVATE _UNICODE)

target_link_libraries(HeadlessIntegrationTest
    PRIVATE AppRuntime
    PRIVATE Console
    PRIVATE DirectXTK
    PRIVATE ExternalTexture
    PRIVATE GraphicsDevice
    PRIVATE NativeEngine
    PRIVATE ScriptLoader
    PRIVATE Window
    PRIVATE XMLHttpRequest)

# Copy UMD Babylon scripts to output directory.
foreach(SCRIPT ${BABYLON_SCRIPTS} ${LOCAL_SCRIPTS})
    get_filename_component(SCRIPT_NAME "${SCRIPT}" NAME)
    add_custom_command(
        OUTPUT "${CMAKE_CFG_INTDIR}/Scripts/${SCRIPT_NAME}"
        COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/${SCRIPT}" "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/Scripts/${SCRIPT_NAME}"
        COMMENT "Copying ${SCRIPT_NAME}"
        MAIN_DEPENDENCY "${CMAKE_CURRENT_SOURCE_DIR}/${SCRIPT}")
endforeach()

# Copy assets from IntegrationTest.
file(GLOB INT_TEST_ASSETS "${INTEGRATION_TEST_DIR}/assets/*")
foreach(ASSET ${INT_TEST_ASSETS})
    get_filename_component(ASSET_NAME "${ASSET}" NAME)
    add_custom_command(TARGET HeadlessIntegrationTest POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:HeadlessIntegrationTest>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ASSET}"
            "$<TARGET_FILE_DIR:HeadlessIntegrationTest>/assets/${ASSET_NAME}"
        COMMENT "Copying ${ASSET_NAME}")
endforeach()

# Copy V8 runtime DLLs to the output directory.
add_custom_command(TARGET HeadlessIntegrationTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E $<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:HeadlessIntegrationTest>>,copy,true> $<TARGET_RUNTIME_DLLS:HeadlessIntegrationTest> $<TARGET_FILE_DIR:HeadlessIntegrationTest> COMMAND_EXPAND_LISTS)

set_property(TARGET HeadlessIntegrationTest PROPERTY FOLDER Apps)
