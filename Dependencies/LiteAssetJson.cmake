include_guard(GLOBAL)

get_filename_component(LITE_ASSET_JSON_DIRECTORY
    "${CMAKE_CURRENT_LIST_DIR}/../Core/LiteLayer/json" ABSOLUTE)
file(SHA256 "${LITE_ASSET_JSON_DIRECTORY}/json.hpp" LITE_ASSET_JSON_HEADER_SHA256)
file(SHA256 "${LITE_ASSET_JSON_DIRECTORY}/LICENSE" LITE_ASSET_JSON_LICENSE_SHA256)
if(NOT LITE_ASSET_JSON_HEADER_SHA256 STREQUAL
        "aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63" OR
   NOT LITE_ASSET_JSON_LICENSE_SHA256 STREQUAL
        "c0d068392ea65358b798b8c165103560f06e9e3b38c4ab4e2d8810a7b931af86")
    message(FATAL_ERROR "The approved nlohmann_json 3.12.0 header/license bytes changed.")
endif()

add_library(LiteAssetJson INTERFACE)
add_library(Babylon::LiteAssetJson ALIAS LiteAssetJson)
target_include_directories(LiteAssetJson SYSTEM INTERFACE "${LITE_ASSET_JSON_DIRECTORY}")
set_property(TARGET LiteAssetJson PROPERTY FOLDER Dependencies)
