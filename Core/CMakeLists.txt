add_subdirectory(Graphics)

if(BABYLON_NATIVE_BUILD_LITE_LAYER)
    add_subdirectory(LiteLayer)
endif()

if(BABYLON_NATIVE_BUILD_LITE_SHADER_COMPILER)
    add_subdirectory(LiteShaderCompiler)
endif()
