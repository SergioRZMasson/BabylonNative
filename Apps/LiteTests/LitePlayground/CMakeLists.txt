if(NOT WIN32 OR WINDOWS_STORE)
    message(FATAL_ERROR "The initial LitePlayground host currently supports Win32.")
endif()

set(SOURCES
    "Source/App.cpp"
    "Source/NativePlatform.h"
    "Source/NativePlatform.cpp"
    "Source/NativeAudio.h"
    "Source/NativeAudio.cpp")
if(TARGET LiteShaderCompiler)
    list(APPEND SOURCES "Source/MinecraftApp.cpp")
endif()
set(DIAGNOSTICS_SOURCES
    "${PROJECT_SOURCE_DIR}/Apps/Playground/Shared/Diagnostics.h"
    "${PROJECT_SOURCE_DIR}/Apps/Playground/Shared/Diagnostics.cpp")

set(LITE_JAVASCRIPT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/JavaScript")
file(SHA256 "${LITE_JAVASCRIPT_DIR}/package-lock.json" LITE_PACKAGE_LOCK_HASH)
if(NOT LITE_PACKAGE_LOCK_HASH STREQUAL BABYLON_LITE_INSTALLED_PACKAGE_LOCK_HASH OR
    NOT EXISTS "${LITE_JAVASCRIPT_DIR}/node_modules/esbuild/package.json")
    npm(ci --ignore-scripts --no-audit --no-fund WORKING_DIRECTORY "${LITE_JAVASCRIPT_DIR}")
    set(BABYLON_LITE_INSTALLED_PACKAGE_LOCK_HASH "${LITE_PACKAGE_LOCK_HASH}" CACHE INTERNAL "")
endif()
find_program(LITE_NODE_EXECUTABLE NAMES node REQUIRED)
file(GLOB_RECURSE LITE_APPLICATION_SOURCES CONFIGURE_DEPENDS
    "${LITE_JAVASCRIPT_DIR}/Demos/*.ts"
    "${LITE_JAVASCRIPT_DIR}/Tests/*.ts"
    "${LITE_JAVASCRIPT_DIR}/Platform/*.js"
    "${LITE_JAVASCRIPT_DIR}/tools/*.mjs"
    "${PROJECT_SOURCE_DIR}/Plugins/LiteJSBinding/JavaScript/*.js")
set(LITE_BUNDLE_OUTPUTS)
foreach(LITE_BUNDLE IN ITEMS minecraft contract-tests minecraft-shaders callback-throw platform)
    list(APPEND LITE_BUNDLE_OUTPUTS "${LITE_JAVASCRIPT_DIR}/dist/${LITE_BUNDLE}.native.js")
endforeach()
list(APPEND LITE_BUNDLE_OUTPUTS "${LITE_JAVASCRIPT_DIR}/dist/coverage.json")
add_custom_command(OUTPUT ${LITE_BUNDLE_OUTPUTS}
    COMMAND "${LITE_NODE_EXECUTABLE}" "${LITE_JAVASCRIPT_DIR}/tools/build.mjs"
    WORKING_DIRECTORY "${LITE_JAVASCRIPT_DIR}"
    DEPENDS ${LITE_APPLICATION_SOURCES} "${LITE_JAVASCRIPT_DIR}/package-lock.json"
    COMMENT "Bundling original user applications without engine implementation")
add_custom_target(LiteApplicationBundles DEPENDS ${LITE_BUNDLE_OUTPUTS})

add_executable(LitePlayground ${SOURCES} ${DIAGNOSTICS_SOURCES})
add_dependencies(LitePlayground LiteApplicationBundles)
warnings_as_errors(LitePlayground)
if(NAPI_JAVASCRIPT_ENGINE STREQUAL "V8")
    target_compile_definitions(LitePlayground PRIVATE LITE_V8_GC_TEST)
endif()
target_include_directories(LitePlayground PRIVATE "${PROJECT_SOURCE_DIR}/Apps/Playground/Shared")
target_link_libraries(LitePlayground PRIVATE LiteJSBinding AppRuntime Console ScriptLoader bx bgfx windowscodecs comdlg32 xaudio2)
target_compile_definitions(LitePlayground PRIVATE
    LITE_SCRIPT_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/JavaScript/dist")
target_compile_definitions(LitePlayground PRIVATE
    LITE_ASSET_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/Assets")
if(TARGET LiteShaderCompiler)
    target_link_libraries(LitePlayground PRIVATE LiteShaderCompiler)
    target_compile_definitions(LitePlayground PRIVATE HAS_LITE_SHADER_COMPILER)
    add_test(NAME LiteMinecraftShaderFixtures COMMAND LitePlayground minecraft-shaders)
    add_test(NAME LiteJavaScriptCallbackThrow COMMAND LitePlayground callback-throw)
    option(BABYLON_LITE_ENABLE_MINECRAFT_TESTS "Run full visible radius-6 Minecraft host smoke tests." ON)
    if(BABYLON_LITE_ENABLE_MINECRAFT_TESTS)
        add_test(NAME LiteMinecraftFull180 COMMAND LitePlayground minecraft --frames=180
            "--capture-root=${CMAKE_CURRENT_BINARY_DIR}/Captures/Full180")
        add_test(NAME LiteMinecraftReplay240 COMMAND LitePlayground minecraft --frames=240 --replay
            "--capture-root=${CMAKE_CURRENT_BINARY_DIR}/Captures/Replay240")
        add_test(NAME LiteMinecraftNativeInputSaveLoad COMMAND LitePlayground minecraft --frames=240 --test-save-load
            "--capture-root=${CMAKE_CURRENT_BINARY_DIR}/Captures/NativeInputSaveLoad")
        set_tests_properties(LiteMinecraftFull180 LiteMinecraftReplay240 LiteMinecraftNativeInputSaveLoad
            PROPERTIES RUN_SERIAL TRUE TIMEOUT 180 LABELS "LiteMinecraft;Graphics;VisibleHost")
    endif()
endif()
add_test(NAME LiteJavaScriptContract COMMAND LitePlayground contract)
add_custom_command(TARGET LitePlayground POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E $<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:LitePlayground>>,copy,true>
        $<TARGET_RUNTIME_DLLS:LitePlayground> $<TARGET_FILE_DIR:LitePlayground> COMMAND_EXPAND_LISTS)
set_property(TARGET LitePlayground PROPERTY FOLDER Apps)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})
