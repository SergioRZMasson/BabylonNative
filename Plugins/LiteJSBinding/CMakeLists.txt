set(SOURCES
    "Include/Babylon/Plugins/LiteJSBinding.h"
    "Source/AudioBinding.h"
    "Source/AudioBinding.cpp"
    "Source/LiteJSBinding.cpp")
add_library(LiteJSBinding STATIC ${SOURCES})
warnings_as_errors(LiteJSBinding)
target_include_directories(LiteJSBinding PUBLIC "Include")
target_link_libraries(LiteJSBinding PUBLIC napi LiteLayer)
set_property(TARGET LiteJSBinding PROPERTY FOLDER Plugins)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})
