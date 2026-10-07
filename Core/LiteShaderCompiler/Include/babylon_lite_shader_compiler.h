#ifndef BABYLON_LITE_SHADER_COMPILER_H
#define BABYLON_LITE_SHADER_COMPILER_H

#include <babylon_lite.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /*
 * Separate host service: runtime Tint WGSL reader/SPIR-V writer, SPIRV-Cross
 * reflection/HLSL and system FXC. Does not create a renderer or depend on
 * LiteLayer. The current service supports D3D11 bgfx shader container v12.
 * The returned table is stateless and may outlive any individual runtime.
 */
    bl_ShaderCompilerService bl_shaderCompilerService(void);

#ifdef __cplusplus
}
#endif

#endif
