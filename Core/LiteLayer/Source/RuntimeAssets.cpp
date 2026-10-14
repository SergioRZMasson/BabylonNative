#include "RuntimeInternal.h"

bl_Status bl_createRuntime2(const bl_RuntimeOptions2* options, bl_Runtime** out)
{
    if (l_inHost())
    {
        return BL_BUSY;
    }
    if (!options || !out || options->structSize != sizeof(*options) ||
        options->version != BL_RUNTIME_OPTIONS_VERSION_2)
    {
        return BL_INVALID_ARGUMENT;
    }
    const bl_AssetService* assets = options->assets;
    const bl_ShaderCompilerService2* compiler = options->shaderCompiler2;
    if (assets && (assets->structSize != sizeof(*assets) ||
                   (assets->version != BL_ASSET_SERVICE_VERSION &&
                    assets->version != BL_ASSET_SERVICE_VERSION_3 &&
                    assets->version != BL_ASSET_SERVICE_VERSION_4 &&
                    assets->version != BL_ASSET_SERVICE_VERSION_5) ||
                   !assets->parseGltf || !assets->releaseGltf || !assets->parseEnvironment ||
                   !assets->releaseEnvironment))
    {
        return BL_INVALID_ARGUMENT;
    }
    if (compiler && (compiler->structSize != sizeof(*compiler) ||
                     compiler->version != BL_SHADER_COMPILER_SERVICE_VERSION_2 ||
                     !compiler->compile || !compiler->release))
    {
        return BL_INVALID_ARGUMENT;
    }
    bl_Runtime* runtime = NULL;
    L_TRY(bl_createRuntime(&options->base, &runtime));
    if (assets)
    {
        runtime->assets = *assets;
        runtime->hasAssets = true;
    }
    if (compiler)
    {
        runtime->compiler2 = *compiler;
        runtime->hasCompiler2 = true;
    }
    *out = runtime;
    return BL_OK;
}
