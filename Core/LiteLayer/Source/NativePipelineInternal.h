#ifndef BL_NATIVE_PIPELINE_INTERNAL_H
#define BL_NATIVE_PIPELINE_INTERNAL_H

#include "LiteInternal.h"

struct L_PipelineBlock
{
    bl_String name;
    uint32_t byteSize;
    uint32_t group;
    uint32_t binding;
};

struct L_PipelineUniform
{
    uint32_t block;
    uint32_t offset;
    uint32_t bytes;
    size_t nativeSlot;
    uint32_t nativeOffset;
};

struct L_PipelineTexture
{
    bl_String name;
    bgfx::UniformHandle uniform;
    uint8_t stage;
    bl_ShaderTextureDimension2 dimension;
    uint32_t operations;
};

struct L_NativePipeline
{
    bgfx::ProgramHandle program;
    L_NativeUniform* uniforms;
    size_t uniformCount;
    L_PipelineUniform* maps;
    size_t mapCount;
    L_PipelineBlock* blocks;
    size_t blockCount;
    L_PipelineTexture* textures;
    size_t textureCount;
};

bl_Status l_compileNativePipeline(bl_Runtime* runtime, L_Engine* engine, bl_String vertex,
                                  bl_String fragment,
                                  const bl_InstanceAttributeDeclaration* instances,
                                  size_t instanceCount, L_NativePipeline* out);
void l_freeNativePipeline(bl_Runtime* runtime, L_NativePipeline* pipeline);
bl_Status l_bindNativeBlocks(bl_Runtime* runtime, L_NativePipeline* pipeline,
                             const bl_Bytes* blocks, size_t count);

#endif
