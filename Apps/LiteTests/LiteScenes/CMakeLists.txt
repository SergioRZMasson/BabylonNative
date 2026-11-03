option(BABYLON_NATIVE_BUILD_LITE_SCENES "Build the reusable C99/native-JS scene host." OFF)
if(NOT BABYLON_NATIVE_BUILD_LITE_SCENES)
    return()
endif()
if(NOT WIN32 OR WINDOWS_STORE OR NOT TARGET LiteAssetDecoder OR NOT BABYLON_LITE_ENABLE_GLTF)
    message(FATAL_ERROR "LiteScenes requires desktop Windows, LiteAssetDecoder and native glTF.")
endif()
if(BABYLON_LITE_ENABLE_STYLE_CHECKS)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

set(LITE_SCENES_CORPUS "" CACHE PATH "Read-only bblitec checkout containing the pinned scenes.")
set(LITE_SCENES_AUDIT "" CACHE PATH "api-coverage-v1 input directory.")
set(LITE_SCENES_DATA "${CMAKE_CURRENT_BINARY_DIR}/data")
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
find_program(LITE_SCENES_NODE NAMES node REQUIRED)
find_path(LITE_SCENES_RENDERDOC_INCLUDE renderdoc_app.h HINTS "C:/Program Files/RenderDoc")
if(NOT LITE_SCENES_CORPUS OR NOT LITE_SCENES_AUDIT)
    message(FATAL_ERROR "Set LITE_SCENES_CORPUS and LITE_SCENES_AUDIT; no assets are downloaded.")
endif()
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/prepare.py"
        --corpus "${LITE_SCENES_CORPUS}" --audit "${LITE_SCENES_AUDIT}"
        --output "${LITE_SCENES_DATA}"
    COMMAND_ERROR_IS_FATAL ANY)
file(GLOB_RECURSE _scene_sources CONFIGURE_DEPENDS "${LITE_SCENES_DATA}/source/*.ts")
add_custom_command(OUTPUT "${LITE_SCENES_DATA}/bundles.json"
    COMMAND "${LITE_SCENES_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/bundle.mjs"
        "${LITE_SCENES_DATA}"
    DEPENDS bundle.mjs platform.js projection-tests.js ${_scene_sources}
        "${PROJECT_SOURCE_DIR}/Plugins/LiteJSBinding/JavaScript/babylon-lite.js"
    VERBATIM)
add_custom_target(LiteScenesBundles DEPENDS "${LITE_SCENES_DATA}/bundles.json")

foreach(_language IN ITEMS Native Js)
    add_executable(LiteScenes${_language} Host.cpp AssetIO.cpp PresentProbe.cpp)
    target_compile_features(LiteScenes${_language} PRIVATE cxx_std_20)
    target_compile_definitions(LiteScenes${_language} PRIVATE
        LITE_SCENES_DATA="${LITE_SCENES_DATA}" WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(LiteScenes${_language} PRIVATE LiteLayer LiteShaderCompiler
        LiteAssetDecoder bgfx user32 psapi windowscodecs ole32 dxgi d3d11)
    warnings_as_errors(LiteScenes${_language})
    if(LITE_SCENES_RENDERDOC_INCLUDE)
        target_include_directories(LiteScenes${_language} SYSTEM PRIVATE "${LITE_SCENES_RENDERDOC_INCLUDE}")
        target_compile_definitions(LiteScenes${_language} PRIVATE LITE_SCENES_RENDERDOC)
    endif()
    if(_language STREQUAL "Js")
        target_compile_definitions(LiteScenesJs PRIVATE LITE_SCENES_JS)
        target_link_libraries(LiteScenesJs PRIVATE LiteJSBinding AppRuntime Console ScriptLoader)
        add_dependencies(LiteScenesJs LiteScenesBundles)
    else()
        target_sources(LiteScenesNative PRIVATE Scenes.cpp)
    endif()
    add_custom_command(TARGET LiteScenes${_language} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E $<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:LiteScenes${_language}>>,copy,true>
            $<TARGET_RUNTIME_DLLS:LiteScenes${_language}> $<TARGET_FILE_DIR:LiteScenes${_language}>
        COMMAND_EXPAND_LISTS)
    foreach(_id IN ITEMS 159 161 163 168 200 247 248 249 256 257 258 259 260 266)
        set(_parity_expectation)
        if(_id EQUAL 200)
            # Approved backend FP-order residual: continuation-v3/summary.json controlledReplay.
            # X-first VS replay is exact0/0; thresholds and production shaders are unchanged.
            set(_parity_expectation --expect-backend-fp-difference)
        endif()
        add_test(NAME LiteScenes${_language}-${_id}
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/harness.py" parity
                --host "$<TARGET_FILE:LiteScenes${_language}>" --data "${LITE_SCENES_DATA}"
                --scene "${_id}" --output "${CMAKE_CURRENT_BINARY_DIR}/parity/${_language}/${_id}"
                ${_parity_expectation})
        set_tests_properties(LiteScenes${_language}-${_id} PROPERTIES
            RUN_SERIAL TRUE TIMEOUT 180 LABELS "LiteScenes;parity")
    endforeach()
endforeach()
add_test(NAME LiteScenesProjectionForwarding COMMAND LiteScenesJs
    --scene=159 "--data=${LITE_SCENES_DATA}"
    "--script=${LITE_SCENES_DATA}/sceneprojection.js" --warmup=0
    "--out=${CMAKE_CURRENT_BINARY_DIR}/projection-tests")
set_tests_properties(LiteScenesProjectionForwarding PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)
add_test(NAME LiteScenesHarnessTests COMMAND "${Python3_EXECUTABLE}" -m unittest
    discover -s "${CMAKE_CURRENT_SOURCE_DIR}" -p "test_*.py")

if(BABYLON_LITE_ENABLE_STYLE_CHECKS)
    find_program(LITE_SCENES_CLANG_TIDY NAMES clang-tidy-22 clang-tidy REQUIRED)
    set(_style_sources
        "${CMAKE_CURRENT_SOURCE_DIR}/Host.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Scenes.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/AssetIO.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/PresentProbe.cpp")
    add_custom_target(LiteScenesStyleCheck
        COMMAND "${LITE_CLANG_FORMAT}" --dry-run --Werror
            "--style=file:${PROJECT_SOURCE_DIR}/Core/LiteLayer/.clang-format"
            ${_style_sources} "${CMAKE_CURRENT_SOURCE_DIR}/Scene.h" "${CMAKE_CURRENT_SOURCE_DIR}/AssetIO.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/PresentProbe.h"
        COMMAND "${LITE_SCENES_CLANG_TIDY}"
            "--config-file=${PROJECT_SOURCE_DIR}/Core/LiteLayer/.clang-tidy"
            "--header-filter=.*[\\\\/]Apps[\\\\/]LiteTests[\\\\/]LiteScenes[\\\\/].*"
            "--extra-arg=-Wno-unused-command-line-argument"
            "-p=${CMAKE_BINARY_DIR}" ${_style_sources}
        VERBATIM)
endif()
