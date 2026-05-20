# HeadlessIntegrationTest reuses the IntegrationTest TypeScript bundle so the
# JavaScript code path is identical to the windowed IntegrationTest.
set(INTEGRATION_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../IntegrationTest")
set(INTEGRATION_TEST_BUNDLE "${INTEGRATION_TEST_DIR}/Scripts/dist/BabylonInterop.bundle.js")

set(SOURCES
    "Source/App.cpp")

add_executable(HeadlessIntegrationTest ${SOURCES})

target_compile_definitions(HeadlessIntegrationTest
    PRIVATE NOMINMAX
    PRIVATE UNICODE
    PRIVATE _UNICODE)

# DirectXTK provides SaveWICTextureToFile for the PNG output. The
# HeadlessScreenshotApp target already fetches it; reuse the same fetched
# target if it's been added, otherwise this app won't compile in
# configurations where HeadlessScreenshotApp is excluded. In our build
# scope (D3D11 + V8 on Windows) HeadlessScreenshotApp is always present.
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

# Reuse the IntegrationTest's JS bundle. IntegrationTestBundle is the target
# that produces it.
add_dependencies(HeadlessIntegrationTest IntegrationTestBundle)

add_custom_command(TARGET HeadlessIntegrationTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:HeadlessIntegrationTest>/Scripts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${INTEGRATION_TEST_BUNDLE}"
        "$<TARGET_FILE_DIR:HeadlessIntegrationTest>/Scripts/BabylonInterop.bundle.js"
    COMMENT "Copying BabylonInterop.bundle.js for HeadlessIntegrationTest")

# Copy the same environment.env IntegrationTest uses.
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
