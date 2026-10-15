include_guard(GLOBAL)
include(FetchContent)

# cgltf 1.15 as shipped by Gary's pinned bgfx source, including bgfx's changes.
# The independent MIT notice is embedded in this unchanged single header.
FetchContent_Declare(LiteCgltfSource
    URL https://raw.githubusercontent.com/bkaradzic/bgfx/cc39a9f963ca73a9b3da25d07c7ead899ec2d71b/3rdparty/cgltf/cgltf.h
    URL_HASH SHA256=efb169dee911696b5d35fc8e3f7ea0c56d679debc529eba9ca6aa6443ba9d5e9
    DOWNLOAD_NO_EXTRACT TRUE
    DOWNLOAD_NAME cgltf.h)
FetchContent_MakeAvailable(LiteCgltfSource)
file(SHA256 "${litecgltfsource_SOURCE_DIR}/cgltf.h" LITE_CGLTF_SHA256)
if(NOT LITE_CGLTF_SHA256 STREQUAL
        "efb169dee911696b5d35fc8e3f7ea0c56d679debc529eba9ca6aa6443ba9d5e9")
    message(FATAL_ERROR "The pinned cgltf header changed.")
endif()
add_library(LiteCgltf INTERFACE)
target_include_directories(LiteCgltf SYSTEM INTERFACE "${litecgltfsource_SOURCE_DIR}")
set_property(TARGET LiteCgltf PROPERTY FOLDER Dependencies)
