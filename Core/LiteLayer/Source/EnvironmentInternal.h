#ifndef BL_ENVIRONMENT_INTERNAL_H
#define BL_ENVIRONMENT_INTERNAL_H

#include "NativePipelineInternal.h"

struct L_Cube
{
    L_Record record;
    L_Engine* engine;
    bl_TextureCubeInfo info;
    bgfx::TextureHandle texture;
    size_t references;
};

struct L_Environment
{
    L_Record record;
    L_Engine* engine;
    bl_EnvironmentInfo info;
    float polynomial[27];
    float harmonics[36];
    L_Environment* next;
    bgfx::TextureHandle* inputs;
    bgfx::FrameBufferHandle* frames;
    size_t resourceCount;
    bgfx::VertexBufferHandle triangle;
    L_NativePipeline decode;
};

bl_Status l_environmentCleanupScene(bl_Runtime* runtime, L_Scene* scene);

#endif
