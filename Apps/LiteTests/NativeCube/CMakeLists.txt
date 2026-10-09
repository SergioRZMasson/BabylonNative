set(LITE_CUBE_STANDARD_DIR "" CACHE PATH "Frozen STANDARD native-cube emission.")
set(LITE_CUBE_BBLITEC_DIR "" CACHE PATH "Existing compiler checkout, language headers only.")
if(NOT LITE_CUBE_STANDARD_DIR OR NOT LITE_CUBE_BBLITEC_DIR)
    return()
endif()
file(SHA256 "${LITE_CUBE_STANDARD_DIR}/main.cpp" _cube_sha)
if(NOT _cube_sha STREQUAL "5a773df02e410ba9831abc279d4bd915e68d05a4a02939e9619d5e72bf057bb7")
    message(FATAL_ERROR "Canonical STANDARD cube user TU changed.")
endif()
set_source_files_properties("${LITE_CUBE_STANDARD_DIR}/main.cpp" PROPERTIES
    COMPILE_DEFINITIONS "main=CubeUserMain")
add_executable(LiteCubeCpp "${LITE_CUBE_STANDARD_DIR}/main.cpp" "Host.cpp" "Binding.cpp")
target_include_directories(LiteCubeCpp BEFORE PRIVATE "Binding" ".")
target_include_directories(LiteCubeCpp SYSTEM PRIVATE "${LITE_CUBE_BBLITEC_DIR}/native/include")
target_link_libraries(LiteCubeCpp PRIVATE LiteLayer LiteShaderCompiler bgfx user32 psapi)
target_include_directories(LiteCubeCpp PRIVATE "${PROJECT_SOURCE_DIR}/Core/LiteLayer/Source")
target_compile_features(LiteCubeCpp PRIVATE cxx_std_20)
target_compile_definitions(LiteCubeCpp PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_options(LiteCubeCpp PRIVATE "/MAP:${CMAKE_CURRENT_BINARY_DIR}/LiteCubeCpp.map")
warnings_as_errors(LiteCubeCpp)
add_executable(LiteCubeJs "Host.cpp")
target_compile_definitions(LiteCubeJs PRIVATE CUBE_JAVASCRIPT _CRT_SECURE_NO_WARNINGS
    CUBE_BUNDLE_FILE="${CMAKE_CURRENT_BINARY_DIR}/native-cube.native.js")
target_compile_features(LiteCubeJs PRIVATE cxx_std_20)
target_include_directories(LiteCubeJs PRIVATE "${PROJECT_SOURCE_DIR}/Core/LiteLayer/Source")
target_link_libraries(LiteCubeJs PRIVATE LiteJSBinding LiteShaderCompiler bgfx
    AppRuntime Console ScriptLoader user32 psapi)
target_link_options(LiteCubeJs PRIVATE "/MAP:${CMAKE_CURRENT_BINARY_DIR}/LiteCubeJs.map")
warnings_as_errors(LiteCubeJs)
find_program(_cube_node NAMES node REQUIRED)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/native-cube.native.js"
    COMMAND "${_cube_node}" "${CMAKE_CURRENT_SOURCE_DIR}/bundle.mjs"
        "${LITE_CUBE_BBLITEC_DIR}" "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDS "bundle.mjs"
        "${PROJECT_SOURCE_DIR}/Plugins/LiteJSBinding/JavaScript/babylon-lite.js"
        "${LITE_CUBE_BBLITEC_DIR}/examples/native-cube.ts")
add_custom_target(LiteCubeBundle DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/native-cube.native.js")
add_dependencies(LiteCubeJs LiteCubeBundle)
add_custom_command(TARGET LiteCubeJs POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E $<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:LiteCubeJs>>,copy,true>
        $<TARGET_RUNTIME_DLLS:LiteCubeJs> $<TARGET_FILE_DIR:LiteCubeJs> COMMAND_EXPAND_LISTS)
