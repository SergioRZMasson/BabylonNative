#ifndef BL_THIN_CULLING_INTERNAL_H
#define BL_THIN_CULLING_INTERNAL_H

#include "NativePipelineInternal.h"

struct L_ThinPipelines
{
    L_ComputePipeline reset;
    L_ComputePipeline cull[2];
    bool ready[2];
    bl_Mat4 viewProjection;
    uint64_t viewFrame;
    uint64_t viewScene;
};

struct L_ThinCullState
{
    L_ThinCullState* next;
    uint64_t meshId;
    size_t sequence;
    uint64_t frame;
    bgfx::DynamicVertexBufferHandle visible;
    bgfx::IndirectBufferHandle args;
    size_t capacity;
    uint16_t stride;
};

#endif
