set(LITE_SCENE38_STANDARD_DIR "" CACHE PATH "Frozen ORIGINAL scene38 STANDARD emission.")
set(LITE_SCENE38_BBLITEC_DIR "" CACHE PATH "External compiler user-language headers only.")
if(NOT LITE_SCENE38_STANDARD_DIR OR NOT LITE_SCENE38_BBLITEC_DIR)
    return()
endif()
file(SHA256 "${LITE_SCENE38_STANDARD_DIR}/main.cpp" _scene38_sha)
if(NOT _scene38_sha STREQUAL "28ebc95c477aac0380cac4a1faa7a7539c306d0addd74a49027ba0b73f9a1e05")
    message(FATAL_ERROR "Canonical byte-preserved ORIGINAL scene38 user TU changed.")
endif()
set_source_files_properties("${LITE_SCENE38_STANDARD_DIR}/main.cpp" PROPERTIES
    COMPILE_DEFINITIONS "main=Scene38UserMain")
add_executable(LiteScene38Cpp "${LITE_SCENE38_STANDARD_DIR}/main.cpp" Host.cpp Binding.cpp)
target_include_directories(LiteScene38Cpp BEFORE PRIVATE Binding ".")
target_include_directories(LiteScene38Cpp SYSTEM PRIVATE "${LITE_SCENE38_BBLITEC_DIR}/native/include")
target_link_libraries(LiteScene38Cpp PRIVATE LiteLayer LiteShaderCompiler bgfx user32 psapi)
target_include_directories(LiteScene38Cpp PRIVATE "${PROJECT_SOURCE_DIR}/Core/LiteLayer/Source")
target_compile_features(LiteScene38Cpp PRIVATE cxx_std_20)
target_compile_definitions(LiteScene38Cpp PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_options(LiteScene38Cpp PRIVATE "/MAP:${CMAKE_CURRENT_BINARY_DIR}/LiteScene38Cpp.map")
warnings_as_errors(LiteScene38Cpp)
add_executable(LiteScene38Js Host.cpp)
target_compile_definitions(LiteScene38Js PRIVATE SCENE38_JAVASCRIPT _CRT_SECURE_NO_WARNINGS
    SCENE38_BUNDLE_FILE="${CMAKE_CURRENT_BINARY_DIR}/scene38.native.js")
target_compile_features(LiteScene38Js PRIVATE cxx_std_20)
target_include_directories(LiteScene38Js PRIVATE "${PROJECT_SOURCE_DIR}/Core/LiteLayer/Source")
target_link_libraries(LiteScene38Js PRIVATE LiteJSBinding LiteShaderCompiler bgfx
    AppRuntime Console ScriptLoader user32 psapi)
target_link_options(LiteScene38Js PRIVATE "/MAP:${CMAKE_CURRENT_BINARY_DIR}/LiteScene38Js.map")
warnings_as_errors(LiteScene38Js)
find_program(_scene38_node NAMES node REQUIRED)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/scene38.native.js"
    COMMAND "${_scene38_node}" "${CMAKE_CURRENT_SOURCE_DIR}/bundle.mjs"
        "${LITE_SCENE38_BBLITEC_DIR}" "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDS bundle.mjs "${PROJECT_SOURCE_DIR}/Plugins/LiteJSBinding/JavaScript/babylon-lite.js"
        "${LITE_SCENE38_BBLITEC_DIR}/corpus/babylon-lite/lab/lite/src/lite/scene38.ts")
add_custom_target(LiteScene38Bundle DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/scene38.native.js")
add_dependencies(LiteScene38Js LiteScene38Bundle)
add_custom_command(TARGET LiteScene38Js POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E $<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:LiteScene38Js>>,copy,true>
        $<TARGET_RUNTIME_DLLS:LiteScene38Js> $<TARGET_FILE_DIR:LiteScene38Js> COMMAND_EXPAND_LISTS)
