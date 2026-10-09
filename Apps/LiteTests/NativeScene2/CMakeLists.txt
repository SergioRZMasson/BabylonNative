set(LITE_SCENE2_STANDARD_DIR "" CACHE PATH "Immutable original scene2 STANDARD emission.")
set(LITE_SCENE2_BBLITEC_DIR "" CACHE PATH "Compiler user headers only.")
if(NOT LITE_SCENE2_STANDARD_DIR OR NOT LITE_SCENE2_BBLITEC_DIR)
    return()
endif()
file(SHA256 "${LITE_SCENE2_STANDARD_DIR}/main.cpp" _scene2_sha)
if(NOT _scene2_sha STREQUAL "074a0cc69325293374be2a291bee015c32cdd6343598742d9b935d8fb68a4649")
    message(FATAL_ERROR "Canonical unchanged original scene2 STANDARD user TU changed.")
endif()
set_source_files_properties("${LITE_SCENE2_STANDARD_DIR}/main.cpp" PROPERTIES
    COMPILE_DEFINITIONS "main=Scene2UserMain")
add_executable(LiteScene2Cpp "${LITE_SCENE2_STANDARD_DIR}/main.cpp" Host.cpp Binding.cpp)
target_include_directories(LiteScene2Cpp BEFORE PRIVATE Binding ".")
target_include_directories(LiteScene2Cpp SYSTEM PRIVATE "${LITE_SCENE2_BBLITEC_DIR}/native/include")
target_link_libraries(LiteScene2Cpp PRIVATE LiteLayer LiteShaderCompiler bgfx user32 psapi)
target_include_directories(LiteScene2Cpp PRIVATE "${PROJECT_SOURCE_DIR}/Core/LiteLayer/Source")
target_compile_features(LiteScene2Cpp PRIVATE cxx_std_20)
target_compile_definitions(LiteScene2Cpp PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_options(LiteScene2Cpp PRIVATE "/MAP:${CMAKE_CURRENT_BINARY_DIR}/LiteScene2Cpp.map")
warnings_as_errors(LiteScene2Cpp)
add_executable(LiteScene2Js Host.cpp)
target_compile_definitions(LiteScene2Js PRIVATE SCENE2_JAVASCRIPT _CRT_SECURE_NO_WARNINGS
    SCENE2_BUNDLE_FILE="${CMAKE_CURRENT_BINARY_DIR}/scene2.native.js")
target_compile_features(LiteScene2Js PRIVATE cxx_std_20)
target_include_directories(LiteScene2Js PRIVATE "${PROJECT_SOURCE_DIR}/Core/LiteLayer/Source")
target_link_libraries(LiteScene2Js PRIVATE LiteJSBinding LiteShaderCompiler bgfx
    AppRuntime Console ScriptLoader user32 psapi)
target_link_options(LiteScene2Js PRIVATE "/MAP:${CMAKE_CURRENT_BINARY_DIR}/LiteScene2Js.map")
warnings_as_errors(LiteScene2Js)
find_program(_scene2_node NAMES node REQUIRED)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/scene2.native.js"
    COMMAND "${_scene2_node}" "${CMAKE_CURRENT_SOURCE_DIR}/bundle.mjs"
        "${LITE_SCENE2_BBLITEC_DIR}" "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDS bundle.mjs "${PROJECT_SOURCE_DIR}/Plugins/LiteJSBinding/JavaScript/babylon-lite.js"
        "${LITE_SCENE2_BBLITEC_DIR}/corpus/babylon-lite/lab/lite/src/lite/scene2.ts")
add_custom_target(LiteScene2Bundle DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/scene2.native.js")
add_dependencies(LiteScene2Js LiteScene2Bundle)
add_custom_command(TARGET LiteScene2Js POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E $<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:LiteScene2Js>>,copy,true>
        $<TARGET_RUNTIME_DLLS:LiteScene2Js> $<TARGET_FILE_DIR:LiteScene2Js> COMMAND_EXPAND_LISTS)
