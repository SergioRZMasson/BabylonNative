if(USD_SDK_PATH)
    set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${USD_SDK_PATH})
    find_package(pxr CONFIG REQUIRED)

    set(SOURCES
    "Include/Babylon/Plugins/USDImporter.h"
    "Source/USDImporter.cpp"
    "Source/USDImporter.h")

    add_library(USDImporter ${SOURCES})
    warnings_as_errors(USDImporter)

    target_include_directories(USDImporter PUBLIC "Include" PRIVATE ${PXR_INCLUDE_DIRS} "Source")

    target_link_to_dependencies(USDImporter
    PUBLIC JsRuntime
    PRIVATE arcana
    PRIVATE napi_extensions
    PRIVATE ${PXR_LIBRARIES})

    set_property(TARGET USDImporter PROPERTY FOLDER Plugins)
    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})

    # USD Warnings

    set(MSVC_FLAGS
    # disable warnings
    /wd4244
    /wd4267
    /wd4273
    /wd4305
    /wd4506
    /wd4996
    /wd4180
    /wd4100
    /wd4201
    /wd4127
    # exporting STL classes
    /wd4251)

    set(MSVC_DEFINITIONS
    # Make sure WinDef.h does not define min and max macros which
    # will conflict with std::min() and std::max().
    NOMINMAX)

    target_compile_options(USDImporter PRIVATE ${MSVC_FLAGS})
    target_compile_definitions(USDImporter PRIVATE ${MSVC_DEFINITIONS})

else()
    message(WARNING "USD_SDK_PATH must be provided for USD Importer to be compiled.")
endif()


