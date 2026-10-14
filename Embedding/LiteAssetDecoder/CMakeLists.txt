include("${PROJECT_SOURCE_DIR}/Dependencies/LiteAssetJson.cmake")

add_library(LiteAssetDecoder STATIC "Source/JsonAdapter.cpp")
add_library(Babylon::LiteAssetDecoder ALIAS LiteAssetDecoder)
target_compile_features(LiteAssetDecoder PRIVATE cxx_std_17)
target_include_directories(LiteAssetDecoder PUBLIC "Include")
target_include_directories(LiteAssetDecoder PRIVATE
    "${PROJECT_SOURCE_DIR}/Core/LiteLayer/Include")
target_link_libraries(LiteAssetDecoder PRIVATE Babylon::LiteAssetJson)
warnings_as_errors(LiteAssetDecoder)
set_property(TARGET LiteAssetDecoder PROPERTY FOLDER Embedding)
