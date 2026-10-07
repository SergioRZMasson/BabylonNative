set(BABYLON_LITE_BBLITEC_SOURCE_DIR "" CACHE PATH "Existing bblitec checkout for application-only native projection tests.")
if(NOT BABYLON_LITE_BBLITEC_SOURCE_DIR)
    return()
endif()
if(NOT EXISTS "${BABYLON_LITE_BBLITEC_SOURCE_DIR}/dist/src/compiler.js")
    message(FATAL_ERROR "Build the selected bblitec checkout first; the native projection does not install or modify it.")
endif()
find_program(LITE_PROJECTION_NODE NAMES node REQUIRED)
set(LITE_PROJECTED_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Generated")
set(LITE_PROJECTED_CPP "${LITE_PROJECTED_DIRECTORY}/Application.cpp")
set(LITE_PROJECTED_HEADER "${LITE_PROJECTED_DIRECTORY}/Application.h")
add_custom_command(OUTPUT "${LITE_PROJECTED_CPP}" "${LITE_PROJECTED_HEADER}" "${LITE_PROJECTED_DIRECTORY}/provenance.json"
    COMMAND "${LITE_PROJECTION_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/Tools/project.mjs"
        "${BABYLON_LITE_BBLITEC_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/Application.ts"
        "${LITE_PROJECTED_DIRECTORY}"
    DEPENDS "Tools/project.mjs" "Source/Application.ts" "Source/C99Projection.cpp"
        "${BABYLON_LITE_BBLITEC_SOURCE_DIR}/dist/.build-stamp"
        "${PROJECT_SOURCE_DIR}/Core/LiteLayer/Include/babylon_lite.h"
    COMMENT "Projecting only test application code onto the C99 native layer")
add_executable(LiteNativeProjectedTests "${LITE_PROJECTED_CPP}" "${LITE_PROJECTED_HEADER}"
    "Source/UserSemantics.h" "Source/C99Projection.h" "Source/C99Projection.cpp" "Source/Tests.cpp")
target_include_directories(LiteNativeProjectedTests PRIVATE "Source" "${LITE_PROJECTED_DIRECTORY}")
target_link_libraries(LiteNativeProjectedTests PRIVATE LiteLayer gtest_main)
warnings_as_errors(LiteNativeProjectedTests)
add_test(NAME LiteNativeProjectedTests COMMAND LiteNativeProjectedTests)
set_property(TARGET LiteNativeProjectedTests PROPERTY FOLDER Apps)
