set(LITE_PRIMITIVES_STANDARD_DIR "" CACHE PATH "Immutable STANDARD primitives emission.")
set(LITE_PRIMITIVES_BBLITEC_DIR "" CACHE PATH "Compiler user headers only.")
if(NOT LITE_PRIMITIVES_STANDARD_DIR OR NOT LITE_PRIMITIVES_BBLITEC_DIR)
    return()
endif()
file(SHA256 "${LITE_PRIMITIVES_STANDARD_DIR}/main.cpp" _primitives_sha)
if(NOT _primitives_sha STREQUAL "831f7a2323bc12a70ed998d0b357685fbdecb8bab0099d8d600721b3be8fa5eb")
    message(FATAL_ERROR "Canonical unchanged STANDARD primitives user TU changed.")
endif()
set_source_files_properties("${LITE_PRIMITIVES_STANDARD_DIR}/main.cpp" PROPERTIES
    COMPILE_DEFINITIONS "main=PrimitivesUserMain")
add_executable(LitePrimitivesCpp "${LITE_PRIMITIVES_STANDARD_DIR}/main.cpp" Host.cpp Binding.cpp)
target_include_directories(LitePrimitivesCpp BEFORE PRIVATE Binding ".")
target_include_directories(LitePrimitivesCpp SYSTEM PRIVATE "${LITE_PRIMITIVES_BBLITEC_DIR}/native/include")
target_link_libraries(LitePrimitivesCpp PRIVATE LiteLayer LiteShaderCompiler bgfx user32 psapi)
target_include_directories(LitePrimitivesCpp PRIVATE "${PROJECT_SOURCE_DIR}/Core/LiteLayer/Source")
target_compile_features(LitePrimitivesCpp PRIVATE cxx_std_20)
target_compile_definitions(LitePrimitivesCpp PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_options(LitePrimitivesCpp PRIVATE "/MAP:${CMAKE_CURRENT_BINARY_DIR}/LitePrimitivesCpp.map")
warnings_as_errors(LitePrimitivesCpp)
add_executable(LitePrimitivesJs Host.cpp)
target_compile_definitions(LitePrimitivesJs PRIVATE PRIMITIVES_JAVASCRIPT _CRT_SECURE_NO_WARNINGS
    PRIMITIVES_BUNDLE_FILE="${CMAKE_CURRENT_BINARY_DIR}/primitives.native.js")
target_compile_features(LitePrimitivesJs PRIVATE cxx_std_20)
target_include_directories(LitePrimitivesJs PRIVATE "${PROJECT_SOURCE_DIR}/Core/LiteLayer/Source")
target_link_libraries(LitePrimitivesJs PRIVATE LiteJSBinding LiteShaderCompiler bgfx
    AppRuntime Console ScriptLoader user32 psapi)
target_link_options(LitePrimitivesJs PRIVATE "/MAP:${CMAKE_CURRENT_BINARY_DIR}/LitePrimitivesJs.map")
warnings_as_errors(LitePrimitivesJs)
find_program(_primitives_node NAMES node REQUIRED)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/primitives.native.js"
    COMMAND "${_primitives_node}" "${CMAKE_CURRENT_SOURCE_DIR}/bundle.mjs"
        "${LITE_PRIMITIVES_BBLITEC_DIR}" "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDS bundle.mjs "${PROJECT_SOURCE_DIR}/Plugins/LiteJSBinding/JavaScript/babylon-lite.js"
        "${LITE_PRIMITIVES_BBLITEC_DIR}/examples/primitives.ts")
add_custom_target(LitePrimitivesBundle DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/primitives.native.js")
add_dependencies(LitePrimitivesJs LitePrimitivesBundle)
add_custom_command(TARGET LitePrimitivesJs POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E $<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:LitePrimitivesJs>>,copy,true>
        $<TARGET_RUNTIME_DLLS:LitePrimitivesJs> $<TARGET_FILE_DIR:LitePrimitivesJs> COMMAND_EXPAND_LISTS)
