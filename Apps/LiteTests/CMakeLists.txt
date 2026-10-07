if(BABYLON_NATIVE_BUILD_LITE_TESTS)
    add_subdirectory(NativeTests)
    add_subdirectory(NativeProjection)
endif()

if(BABYLON_NATIVE_BUILD_LITE_PLAYGROUND)
    add_subdirectory(LitePlayground)
endif()

if(BABYLON_NATIVE_BUILD_LITE_SHADER_TESTS)
    add_subdirectory(ShaderTests)
endif()
