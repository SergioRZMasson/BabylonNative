#include "NativePipelineInternal.h"

void l_freeNativePipeline(bl_Runtime* runtime, L_NativePipeline* pipeline)
{
    if (bgfx::isValid(pipeline->program))
    {
        bgfx::destroy(pipeline->program);
    }
    for (size_t i = 0; i < pipeline->uniformCount; ++i)
    {
        if (bgfx::isValid(pipeline->uniforms[i].handle))
        {
            bgfx::destroy(pipeline->uniforms[i].handle);
        }
        l_free(runtime, (void*)pipeline->uniforms[i].name.data);
        l_free(runtime, pipeline->uniforms[i].bytes);
    }
    for (size_t i = 0; i < pipeline->blockCount; ++i)
    {
        l_free(runtime, (void*)pipeline->blocks[i].name.data);
    }
    for (size_t i = 0; i < pipeline->textureCount; ++i)
    {
        if (bgfx::isValid(pipeline->textures[i].uniform))
        {
            bgfx::destroy(pipeline->textures[i].uniform);
        }
        l_free(runtime, (void*)pipeline->textures[i].name.data);
    }
    l_free(runtime, pipeline->uniforms);
    l_free(runtime, pipeline->blocks);
    l_free(runtime, pipeline->maps);
    l_free(runtime, pipeline->textures);
    *pipeline = {};
    pipeline->program.idx = BL_INVALID_BGFX_HANDLE;
}

static bool nativeStages(uint32_t stages, bool compute = false)
{
    return compute ? stages == BL_STAGE_COMPUTE : stages && !(stages & ~UINT32_C(3));
}

static bool sameTextureBinding(const bl_ReflectedTexture* first, const bl_ReflectedTexture* second)
{
    return l_equal(first->name, second->name) && l_equal(first->samplerName, second->samplerName) &&
           l_equal(first->nativeName, second->nativeName) &&
           first->textureGroup == second->textureGroup &&
           first->textureBinding == second->textureBinding &&
           first->samplerGroup == second->samplerGroup &&
           first->samplerBinding == second->samplerBinding && first->stages == second->stages &&
           first->nativeTextureStage == second->nativeTextureStage;
}

static bl_Status nativeReflection(bl_Runtime* runtime, const bl_ShaderCompileResult2* result,
                                  const bl_InstanceAttributeDeclaration* instances,
                                  size_t instanceCount, L_NativePipeline* pipeline,
                                  bool compute = false)
{
    const bl_ShaderCompileResult* base = &result->base;
    if (result->structSize != sizeof(*result) || result->version != 2 || base->attributeCount > 6 ||
        result->instanceAttributeCount > instanceCount ||
        base->uniformCount > UINT32_MAX / sizeof(L_NativeUniform) ||
        base->uniformBlockCount > UINT32_MAX / sizeof(L_PipelineBlock) ||
        base->textureCount > bgfx::getCaps()->limits.maxTextureSamplers ||
        !l_typedSpan(base->attributes, base->attributeCount, alignof(bl_ReflectedAttribute)) ||
        !l_typedSpan(result->instanceAttributes, result->instanceAttributeCount,
                     alignof(bl_ReflectedInstanceAttribute)) ||
        !l_typedSpan(base->uniformBlocks, base->uniformBlockCount,
                     alignof(bl_ReflectedUniformBlock)) ||
        !l_typedSpan(base->uniforms, base->uniformCount, alignof(bl_ReflectedUniform)) ||
        !l_typedSpan(base->textures, base->textureCount, alignof(bl_ReflectedTexture)) ||
        !l_typedSpan(result->textureInfo, result->textureInfoCount,
                     alignof(bl_ReflectedTexture2)) ||
        result->textureInfoCount != base->textureCount)
    {
        return BL_SHADER_ERROR;
    }
    const uint8_t components[6] = {3, 3, 2, 2, 4, 4};
    for (size_t i = 0; i < base->attributeCount; ++i)
    {
        const bl_ReflectedAttribute* attribute = base->attributes + i;
        if (!l_string(attribute->name) || !attribute->name.length ||
            (unsigned)attribute->semantic >= 6 ||
            attribute->components != components[attribute->semantic])
        {
            return BL_SHADER_ERROR;
        }
        for (size_t j = 0; j < i; ++j)
        {
            if (base->attributes[j].location == attribute->location ||
                base->attributes[j].semantic == attribute->semantic)
            {
                return BL_SHADER_ERROR;
            }
        }
    }
    for (size_t i = 0; i < result->instanceAttributeCount; ++i)
    {
        const bl_ReflectedInstanceAttribute* attribute = result->instanceAttributes + i;
        bool found = false;
        if (!l_string(attribute->name) || attribute->components != 4)
        {
            return BL_SHADER_ERROR;
        }
        for (size_t j = 0; j < instanceCount; ++j)
        {
            const bl_InstanceAttributeDeclaration* declaration = instances + j;
            found |= l_equal(attribute->name, declaration->name) &&
                     attribute->location == declaration->location &&
                     attribute->matrixColumn == declaration->matrixColumn &&
                     attribute->byteOffset == declaration->byteOffset &&
                     attribute->byteStride == declaration->byteStride;
        }
        if (!found)
        {
            return BL_SHADER_ERROR;
        }
        for (size_t j = 0; j < i; ++j)
        {
            if (result->instanceAttributes[j].location == attribute->location ||
                result->instanceAttributes[j].matrixColumn == attribute->matrixColumn)
            {
                return BL_SHADER_ERROR;
            }
        }
        for (size_t j = 0; j < base->attributeCount; ++j)
        {
            if (base->attributes[j].location == attribute->location)
            {
                return BL_SHADER_ERROR;
            }
        }
    }
    size_t bytes;
    if (!l_size(base->uniformCount, sizeof(L_NativeUniform), &bytes))
    {
        return BL_SHADER_ERROR;
    }
    pipeline->uniforms = (L_NativeUniform*)l_alloc(runtime, bytes);
    pipeline->maps =
        (L_PipelineUniform*)l_alloc(runtime, base->uniformCount * sizeof(L_PipelineUniform));
    pipeline->blocks =
        (L_PipelineBlock*)l_alloc(runtime, base->uniformBlockCount * sizeof(L_PipelineBlock));
    pipeline->textures =
        (L_PipelineTexture*)l_alloc(runtime, base->textureCount * sizeof(L_PipelineTexture));
    if ((base->uniformCount && (!pipeline->uniforms || !pipeline->maps)) ||
        (base->uniformBlockCount && !pipeline->blocks) ||
        (base->textureCount && !pipeline->textures))
    {
        return BL_OUT_OF_MEMORY;
    }
    for (size_t i = 0; i < base->uniformBlockCount; ++i)
    {
        const bl_ReflectedUniformBlock* block = base->uniformBlocks + i;
        if (!l_string(block->name) || !block->name.length || !block->byteSize ||
            block->byteSize % 16 || !nativeStages(block->stages, compute))
        {
            return BL_SHADER_ERROR;
        }
        for (size_t j = 0; j < i; ++j)
        {
            if (base->uniformBlocks[j].group == block->group &&
                base->uniformBlocks[j].binding == block->binding)
            {
                return BL_SHADER_ERROR;
            }
        }
        bl_String name = l_copyString(runtime, block->name);
        if (!name.data)
        {
            return BL_OUT_OF_MEMORY;
        }
        pipeline->blocks[pipeline->blockCount++] = {name, block->byteSize, block->group,
                                                    block->binding};
    }
    for (size_t i = 0; i < base->uniformCount; ++i)
    {
        const bl_ReflectedUniform* uniform = base->uniforms + i;
        if (uniform->blockIndex >= base->uniformBlockCount || !l_string(uniform->name) ||
            !uniform->name.length || (unsigned)uniform->type > BL_UNIFORM_MAT4 ||
            uniform->byteSize != uniformCounts[uniform->type] * 4 ||
            uniform->byteOffset % (uniform->type == BL_UNIFORM_VEC2   ? 8u
                                   : uniform->type >= BL_UNIFORM_VEC3 ? 16u
                                                                      : 4u) ||
            uniform->nativeByteOffset % 4 || !nativeStages(uniform->stages, compute) ||
            (uniform->stages & ~base->uniformBlocks[uniform->blockIndex].stages) ||
            !l_string(uniform->nativeName) || !uniform->nativeName.length ||
            !uniform->nativeCount || uniform->nativeType != BL_NATIVE_UNIFORM_VEC4 ||
            uniform->byteOffset > pipeline->blocks[uniform->blockIndex].byteSize ||
            uniform->byteSize >
                pipeline->blocks[uniform->blockIndex].byteSize - uniform->byteOffset ||
            uniform->nativeByteOffset > uniform->nativeCount * 16u ||
            uniform->byteSize > uniform->nativeCount * 16u - uniform->nativeByteOffset)
        {
            return BL_SHADER_ERROR;
        }
        for (size_t j = 0; j < i; ++j)
        {
            const bl_ReflectedUniform* previous = base->uniforms + j;
            if ((previous->blockIndex == uniform->blockIndex &&
                 (l_equal(previous->name, uniform->name) ||
                  (uniform->byteOffset < previous->byteOffset + previous->byteSize &&
                   previous->byteOffset < uniform->byteOffset + uniform->byteSize))) ||
                (l_equal(previous->nativeName, uniform->nativeName) &&
                 (previous->nativeType != uniform->nativeType ||
                  previous->nativeCount != uniform->nativeCount ||
                  (uniform->nativeByteOffset < previous->nativeByteOffset + previous->byteSize &&
                   previous->nativeByteOffset < uniform->nativeByteOffset + uniform->byteSize))))
            {
                return BL_SHADER_ERROR;
            }
        }
        size_t slot = 0;
        while (slot < pipeline->uniformCount &&
               !l_equal(pipeline->uniforms[slot].name, uniform->nativeName))
        {
            ++slot;
        }
        if (slot == pipeline->uniformCount)
        {
            L_NativeUniform* native = pipeline->uniforms + slot;
            native->handle.idx = BL_INVALID_BGFX_HANDLE;
            native->name = l_copyString(runtime, uniform->nativeName);
            native->type = uniform->nativeType;
            native->count = uniform->nativeCount;
            native->byteCount = uniform->nativeCount * 16u;
            native->bytes = (uint8_t*)l_alloc(runtime, native->byteCount);
            ++pipeline->uniformCount;
            if (!native->name.data || !native->bytes)
            {
                return BL_OUT_OF_MEMORY;
            }
            native->handle =
                bgfx::createUniform(native->name.data, bgfx::UniformType::Vec4, native->count);
            if (!bgfx::isValid(native->handle))
            {
                return BL_SHADER_ERROR;
            }
        }
        else if (pipeline->uniforms[slot].count != uniform->nativeCount)
        {
            return BL_SHADER_ERROR;
        }
        pipeline->maps[pipeline->mapCount++] = {uniform->blockIndex, uniform->byteOffset,
                                                uniform->byteSize, slot, uniform->nativeByteOffset};
    }
    for (size_t i = 0; i < base->textureCount; ++i)
    {
        const bl_ReflectedTexture* binding = base->textures + i;
        const bl_ReflectedTexture2* info = result->textureInfo + i;
        if (!l_string(binding->name) || !binding->name.length || !l_string(binding->nativeName) ||
            !binding->nativeName.length || !l_string(binding->samplerName) ||
            !nativeStages(binding->stages) || !l_string(info->binding.name) ||
            !l_string(info->binding.nativeName) || !l_string(info->binding.samplerName) ||
            !sameTextureBinding(binding, &info->binding) ||
            binding->nativeTextureStage >= bgfx::getCaps()->limits.maxTextureSamplers ||
            info->sampleType != BL_SHADER_TEXTURE_FLOAT ||
            (unsigned)info->dimension > BL_SHADER_TEXTURE_CUBE ||
            (unsigned)info->samplerKind > BL_SHADER_SAMPLER_REGULAR || !info->samplingOperations ||
            (info->samplingOperations & ~UINT32_C(127)) ||
            (info->samplerKind == BL_SHADER_SAMPLER_NONE &&
             (binding->samplerName.length || binding->samplerGroup != UINT32_MAX ||
              binding->samplerBinding != UINT32_MAX ||
              (info->samplingOperations & UINT32_C(62)))) ||
            (info->samplerKind == BL_SHADER_SAMPLER_REGULAR &&
             (!binding->samplerName.length ||
              (info->samplingOperations & BL_SHADER_TEXTURE_USE_SAMPLE_COMPARE))))
        {
            return BL_SHADER_ERROR;
        }
        for (size_t j = 0; j < i; ++j)
        {
            const bl_ReflectedTexture* previous = base->textures + j;
            if (previous->nativeTextureStage == binding->nativeTextureStage ||
                (previous->textureGroup == binding->textureGroup &&
                 previous->textureBinding == binding->textureBinding))
            {
                return BL_SHADER_ERROR;
            }
        }
        bl_String nativeName = l_copyString(runtime, binding->nativeName);
        bl_String name = l_copyString(runtime, binding->name);
        if (!nativeName.data || !name.data)
        {
            l_free(runtime, (void*)nativeName.data);
            l_free(runtime, (void*)name.data);
            return BL_OUT_OF_MEMORY;
        }
        bgfx::UniformHandle uniform =
            bgfx::createUniform(nativeName.data, bgfx::UniformType::Sampler);
        l_free(runtime, (void*)nativeName.data);
        if (!bgfx::isValid(uniform))
        {
            l_free(runtime, (void*)name.data);
            return BL_SHADER_ERROR;
        }
        pipeline->textures[pipeline->textureCount++] = {name, uniform, binding->nativeTextureStage,
                                                        info->dimension, info->samplingOperations};
    }
    return BL_OK;
}

bl_Status l_compileNativePipeline(bl_Runtime* runtime, L_Engine* engine, bl_String vertex,
                                  bl_String fragment,
                                  const bl_InstanceAttributeDeclaration* instances,
                                  size_t instanceCount, L_NativePipeline* out)
{
    if (!runtime->hasCompiler2)
    {
        return BL_UNSUPPORTED;
    }
    const bgfx::Caps* caps = bgfx::getCaps();
    bl_ShaderCompileRequest2 request = {sizeof(bl_ShaderCompileRequest2),
                                        BL_SHADER_COMPILE_VERSION_2,
                                        {1,
                                         engine->backend,
                                         12,
                                         {BL_STAGE_VERTEX, vertex, {"main", 4}},
                                         {BL_STAGE_FRAGMENT, fragment, {"main", 4}},
                                         caps->homogeneousDepth,
                                         caps->originBottomLeft,
                                         {"native-runtime-material", 23}},
                                        instances,
                                        instanceCount};
    bl_ShaderCompileResult2 result = {};
    result.structSize = sizeof(result);
    result.version = BL_SHADER_COMPILE_VERSION_2;
    bl_Status status;
    L_CALL(runtime, status,
           runtime->compiler2.compile(runtime->compiler2.userData, &request, &result));
    L_NativePipeline pipeline = {};
    pipeline.program.idx = BL_INVALID_BGFX_HANDLE;
    if (status == BL_OK)
    {
        status = nativeReflection(runtime, &result, instances, instanceCount, &pipeline);
    }
    if (status == BL_OK)
    {
        const bl_ShaderCompileResult* base = &result.base;
        if (!base->vertexContainer.data || base->vertexContainer.count < 12 ||
            base->vertexContainer.count > UINT32_MAX || !base->fragmentContainer.data ||
            base->fragmentContainer.count < 12 || base->fragmentContainer.count > UINT32_MAX ||
            memcmp(base->vertexContainer.data, "VSH", 3) || base->vertexContainer.data[3] != 12 ||
            memcmp(base->fragmentContainer.data, "FSH", 3) ||
            base->fragmentContainer.data[3] != 12 ||
            memcmp(base->vertexContainer.data + 8, base->fragmentContainer.data + 4, 4))
        {
            status = BL_SHADER_ERROR;
        }
        else
        {
            bgfx::ShaderHandle vs = bgfx::createShader(
                bgfx::copy(base->vertexContainer.data, (uint32_t)base->vertexContainer.count));
            bgfx::ShaderHandle fs = bgfx::createShader(
                bgfx::copy(base->fragmentContainer.data, (uint32_t)base->fragmentContainer.count));
            if (bgfx::isValid(vs) && bgfx::isValid(fs))
            {
                pipeline.program = bgfx::createProgram(vs, fs, true);
            }
            else
            {
                if (bgfx::isValid(vs))
                {
                    bgfx::destroy(vs);
                }
                if (bgfx::isValid(fs))
                {
                    bgfx::destroy(fs);
                }
            }
            if (!bgfx::isValid(pipeline.program))
            {
                status = BL_SHADER_ERROR;
            }
        }
    }
    char message[1024] = {};
    if (status != BL_OK && l_string(result.base.diagnostics))
    {
        size_t length = result.base.diagnostics.length < sizeof(message) - 1
                            ? result.base.diagnostics.length
                            : sizeof(message) - 1;
        while (length && length < result.base.diagnostics.length &&
               ((uint8_t)result.base.diagnostics.data[length] & 0xc0) == 0x80)
        {
            --length;
        }
        memcpy(message, result.base.diagnostics.data, length);
    }
    l_enterHost(runtime);
    runtime->compiler2.release(runtime->compiler2.userData, &result);
    l_leaveHost(runtime);
    if (status != BL_OK)
    {
        l_freeNativePipeline(runtime, &pipeline);
        return l_error(runtime, status, __func__, message[0] ? message : "Native shader failure");
    }
    *out = pipeline;
    return BL_OK;
}

bl_Status l_bindNativeBlocks(bl_Runtime* runtime, L_NativePipeline* pipeline,
                             const bl_Bytes* blocks, size_t count)
{
    if (count != pipeline->blockCount)
    {
        return BL_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < count; ++i)
    {
        if (!l_span(blocks[i].data, blocks[i].count) ||
            blocks[i].count != pipeline->blocks[i].byteSize)
        {
            return BL_SHADER_ERROR;
        }
    }
    for (size_t i = 0; i < pipeline->uniformCount; ++i)
    {
        memset(pipeline->uniforms[i].bytes, 0, pipeline->uniforms[i].byteCount);
    }
    for (size_t i = 0; i < pipeline->mapCount; ++i)
    {
        const L_PipelineUniform* map = pipeline->maps + i;
        memcpy(pipeline->uniforms[map->nativeSlot].bytes + map->nativeOffset,
               blocks[map->block].data + map->offset, map->bytes);
    }
    for (size_t i = 0; i < pipeline->uniformCount; ++i)
    {
        bgfx::setUniform(pipeline->uniforms[i].handle, pipeline->uniforms[i].bytes,
                         pipeline->uniforms[i].count);
    }
    (void)runtime;
    return BL_OK;
}

void l_freeComputePipeline(bl_Runtime* r, L_ComputePipeline* pipeline)
{
    l_freeNativePipeline(r, &pipeline->native);
    for (size_t i = 0; i < pipeline->storageCount; ++i)
    {
        l_free(r, (void*)pipeline->storage[i].name.data);
    }
    l_free(r, pipeline->storage);
    *pipeline = {};
    pipeline->native.program.idx = BL_INVALID_BGFX_HANDLE;
}

static bl_Status computeReflection(bl_Runtime* r, const bl_ComputeShaderCompileResult* result,
                                   L_ComputePipeline* pipeline)
{
    const bgfx::Caps* caps = bgfx::getCaps();
    if (result->structSize != sizeof(*result) || result->version != BL_COMPUTE_SHADER_VERSION ||
        !l_typedSpan(result->storageBuffers, result->storageBufferCount,
                     alignof(bl_ReflectedStorageBuffer)) ||
        result->storageBufferCount > caps->limits.maxComputeBindings || !result->container.data ||
        result->container.count < 28 || result->container.count > UINT32_MAX ||
        memcmp(result->container.data, "CSH\x0c", 4) || !result->workgroupSize[0] ||
        !result->workgroupSize[1] || !result->workgroupSize[2] || result->workgroupSize[0] > 1024 ||
        result->workgroupSize[1] > 1024 || result->workgroupSize[2] > 64 ||
        (uint64_t)result->workgroupSize[0] * result->workgroupSize[1] * result->workgroupSize[2] >
            1024)
    {
        return BL_SHADER_ERROR;
    }
    uint32_t srv = 0;
    uint32_t uav = 0;
    for (size_t i = 0; i < result->storageBufferCount; ++i)
    {
        const bl_ReflectedStorageBuffer* buffer = result->storageBuffers + i;
        if (!l_identifier(buffer->name) || !buffer->name.length ||
            buffer->nativeStage >= caps->limits.maxComputeBindings || buffer->nativeStage >= 32 ||
            buffer->minimumBytes > UINT32_MAX ||
            ((srv | uav) & (UINT32_C(1) << buffer->nativeStage)))
        {
            return BL_SHADER_ERROR;
        }
        for (size_t j = 0; j < i; ++j)
        {
            const bl_ReflectedStorageBuffer* previous = result->storageBuffers + j;
            if (l_equal(previous->name, buffer->name) ||
                (previous->group == buffer->group && previous->binding == buffer->binding))
            {
                return BL_SHADER_ERROR;
            }
        }
        if (buffer->readOnly)
        {
            srv |= UINT32_C(1) << buffer->nativeStage;
        }
        else
        {
            uav |= UINT32_C(1) << buffer->nativeStage;
        }
    }
    const uint8_t* bytes = result->container.data;
    uint32_t reflectedSrv = (uint32_t)bytes[12] | (uint32_t)bytes[13] << 8 |
                            (uint32_t)bytes[14] << 16 | (uint32_t)bytes[15] << 24;
    uint32_t reflectedUav = (uint32_t)bytes[16] | (uint32_t)bytes[17] << 8 |
                            (uint32_t)bytes[18] << 16 | (uint32_t)bytes[19] << 24;
    if (srv != reflectedSrv || uav != reflectedUav)
    {
        return BL_SHADER_ERROR;
    }
    bl_ShaderCompileResult2 uniformResult = {};
    uniformResult.structSize = sizeof(uniformResult);
    uniformResult.version = BL_SHADER_COMPILE_VERSION_2;
    uniformResult.base.uniformBlocks = result->uniformBlocks;
    uniformResult.base.uniformBlockCount = result->uniformBlockCount;
    uniformResult.base.uniforms = result->uniforms;
    uniformResult.base.uniformCount = result->uniformCount;
    L_TRY(nativeReflection(r, &uniformResult, NULL, 0, &pipeline->native, true));
    size_t size;
    if (!l_size(result->storageBufferCount, sizeof(bl_ReflectedStorageBuffer), &size))
    {
        return BL_SHADER_ERROR;
    }
    pipeline->storage = (bl_ReflectedStorageBuffer*)l_alloc(r, size);
    if (size && !pipeline->storage)
    {
        return BL_OUT_OF_MEMORY;
    }
    for (size_t i = 0; i < result->storageBufferCount; ++i)
    {
        pipeline->storage[i] = result->storageBuffers[i];
        pipeline->storage[i].name = l_copyString(r, result->storageBuffers[i].name);
        ++pipeline->storageCount;
        if (!pipeline->storage[i].name.data)
        {
            return BL_OUT_OF_MEMORY;
        }
    }
    memcpy(pipeline->workgroupSize, result->workgroupSize, sizeof(pipeline->workgroupSize));
    return BL_OK;
}

bl_Status l_compileComputePipeline(bl_Runtime* r, L_Engine* engine, bl_String source,
                                   bl_String entry, L_ComputePipeline* out)
{
    if (!r->hasComputeCompiler || !(bgfx::getCaps()->supported & BGFX_CAPS_COMPUTE))
    {
        return L_FAIL(r, BL_UNSUPPORTED,
                      "A runtime compute compiler and compute backend are required");
    }
    bl_ComputeShaderCompileRequest request = {
        sizeof(request), BL_COMPUTE_SHADER_VERSION,    engine->backend, 12, source,
        entry,           {"thin-instance-culling", 21}};
    bl_ComputeShaderCompileResult result = {};
    result.structSize = sizeof(result);
    result.version = BL_COMPUTE_SHADER_VERSION;
    L_ComputePipeline pipeline = {};
    pipeline.native.program.idx = BL_INVALID_BGFX_HANDLE;
    bl_Status status;
    r->computeUsed = true;
    L_CALL(r, status, r->computeCompiler.compile(r->computeCompiler.userData, &request, &result));
    if (status == BL_OK)
    {
        status = computeReflection(r, &result, &pipeline);
    }
    if (status == BL_OK)
    {
        bgfx::ShaderHandle shader =
            bgfx::createShader(bgfx::copy(result.container.data, (uint32_t)result.container.count));
        if (bgfx::isValid(shader))
        {
            pipeline.native.program = bgfx::createProgram(shader, true);
        }
        if (!bgfx::isValid(pipeline.native.program))
        {
            status = BL_SHADER_ERROR;
        }
    }
    char diagnostics[1024] = {};
    if (status != BL_OK && l_string(result.diagnostics) && result.diagnostics.length)
    {
        size_t length = result.diagnostics.length < sizeof(diagnostics) - 1
                            ? result.diagnostics.length
                            : sizeof(diagnostics) - 1;
        while (length && length < result.diagnostics.length &&
               ((uint8_t)result.diagnostics.data[length] & 0xc0) == 0x80)
        {
            --length;
        }
        memcpy(diagnostics, result.diagnostics.data, length);
    }
    l_enterHost(r);
    r->computeCompiler.release(r->computeCompiler.userData, &result);
    l_leaveHost(r);
    if (status != BL_OK)
    {
        l_freeComputePipeline(r, &pipeline);
        return L_FAIL(r, status,
                      diagnostics[0] ? diagnostics : "Compute shader preparation failed");
    }
    *out = pipeline;
    return BL_OK;
}
