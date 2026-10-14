set(LITE_SCENE35_STANDARD_DIR "" CACHE PATH "Frozen ORIGINAL scene35 STANDARD emission.")
set(LITE_SCENE35_ORIGINAL_DIR "" CACHE PATH "Frozen ORIGINAL scene35 TypeScript input.")
set(LITE_SCENE35_BBLITEC_DIR "" CACHE PATH "External compiler user-language headers only.")
if(NOT LITE_SCENE35_STANDARD_DIR OR NOT LITE_SCENE35_ORIGINAL_DIR OR NOT LITE_SCENE35_BBLITEC_DIR)
    return()
endif()
if(NOT TARGET LiteAssetDecoder)
    message(FATAL_ERROR "Scene35 requires BABYLON_NATIVE_BUILD_LITE_ASSET_DECODER=ON.")
endif()
file(SHA256 "${LITE_SCENE35_STANDARD_DIR}/main.cpp" _scene35_sha)
if(NOT _scene35_sha STREQUAL "ccc056a37848607c909f8c2098b53484808573ec375656870546a34e36646d65")
    message(FATAL_ERROR "Canonical byte-preserved ORIGINAL scene35 user TU changed.")
endif()
set_source_files_properties("${LITE_SCENE35_STANDARD_DIR}/main.cpp" PROPERTIES
    COMPILE_DEFINITIONS "main=Scene35UserMain")
add_executable(LiteScene35Cpp "${LITE_SCENE35_STANDARD_DIR}/main.cpp" Host.cpp AssetIO.cpp Binding.cpp)
target_include_directories(LiteScene35Cpp BEFORE PRIVATE Binding ".")
target_include_directories(LiteScene35Cpp SYSTEM PRIVATE "${LITE_SCENE35_BBLITEC_DIR}/native/include")
target_link_libraries(LiteScene35Cpp PRIVATE LiteLayer LiteShaderCompiler LiteAssetDecoder
    bgfx user32 psapi windowscodecs ole32)
target_include_directories(LiteScene35Cpp PRIVATE "${PROJECT_SOURCE_DIR}/Core/LiteLayer/Source")
target_compile_features(LiteScene35Cpp PRIVATE cxx_std_20)
target_link_options(LiteScene35Cpp PRIVATE "/MAP:${CMAKE_CURRENT_BINARY_DIR}/LiteScene35Cpp.map")
warnings_as_errors(LiteScene35Cpp)
add_executable(LiteScene35Js Host.cpp AssetIO.cpp)
target_compile_definitions(LiteScene35Js PRIVATE SCENE35_JAVASCRIPT
    SCENE35_BUNDLE_FILE="${CMAKE_CURRENT_BINARY_DIR}/scene35.native.js")
target_include_directories(LiteScene35Js PRIVATE "${PROJECT_SOURCE_DIR}/Core/LiteLayer/Source")
target_compile_features(LiteScene35Js PRIVATE cxx_std_20)
target_link_libraries(LiteScene35Js PRIVATE LiteJSBinding LiteShaderCompiler LiteAssetDecoder bgfx
    AppRuntime Console ScriptLoader user32 psapi windowscodecs ole32)
target_link_options(LiteScene35Js PRIVATE "/MAP:${CMAKE_CURRENT_BINARY_DIR}/LiteScene35Js.map")
warnings_as_errors(LiteScene35Js)
find_program(_scene35_node NAMES node REQUIRED)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/scene35.native.js"
    COMMAND "${_scene35_node}" "${CMAKE_CURRENT_SOURCE_DIR}/bundle.mjs"
        "${LITE_SCENE35_ORIGINAL_DIR}" "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDS bundle.mjs "${PROJECT_SOURCE_DIR}/Plugins/LiteJSBinding/JavaScript/babylon-lite.js"
        "${LITE_SCENE35_ORIGINAL_DIR}/scene35.ts")
add_custom_target(LiteScene35Bundle DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/scene35.native.js")
add_dependencies(LiteScene35Js LiteScene35Bundle)
add_custom_command(TARGET LiteScene35Js POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E $<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:LiteScene35Js>>,copy,true>
        $<TARGET_RUNTIME_DLLS:LiteScene35Js> $<TARGET_FILE_DIR:LiteScene35Js> COMMAND_EXPAND_LISTS)
