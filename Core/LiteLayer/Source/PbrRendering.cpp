#include "PbrInternal.h"

static void cleanupPacket(bl_Runtime* runtime, L_PbrPacket* packet)
{
    if (packet->material)
    {
        l_freeNativePipeline(runtime, &packet->pipeline);
        if (packet->environment)
        {
            l_unpin(&packet->environment->record);
        }
        l_unpin(&packet->material->record);
    }
    *packet = {};
}

void l_removePbrPacket(bl_Runtime* runtime, L_Scene* scene, size_t index)
{
    size_t count =
        scene->memberCount < scene->pbrCapacity ? scene->memberCount : scene->pbrCapacity;
    if (index < count)
    {
        cleanupPacket(runtime, scene->pbrPackets + index);
        memmove(scene->pbrPackets + index, scene->pbrPackets + index + 1,
                (count - index - 1) * sizeof(L_PbrPacket));
        scene->pbrPackets[count - 1] = {};
    }
}

void l_cleanupPbrScene(bl_Runtime* runtime, L_Scene* scene)
{
    for (size_t i = 0; i < scene->pbrCapacity; ++i)
    {
        cleanupPacket(runtime, scene->pbrPackets + i);
    }
    l_free(runtime, scene->pbrPackets);
    scene->pbrPackets = NULL;
    scene->pbrCapacity = 0;
}

static bl_Status reservePackets(bl_Runtime* runtime, L_Scene* scene)
{
    if (scene->memberCount <= scene->pbrCapacity)
    {
        return BL_OK;
    }
    size_t bytes;
    if (!l_size(scene->memberCapacity, sizeof(L_PbrPacket), &bytes))
    {
        return BL_OUT_OF_MEMORY;
    }
    L_PbrPacket* packets = (L_PbrPacket*)l_alloc(runtime, bytes);
    if (!packets)
    {
        return BL_OUT_OF_MEMORY;
    }
    if (scene->pbrCapacity)
    {
        memcpy(packets, scene->pbrPackets, scene->pbrCapacity * sizeof(*packets));
    }
    l_free(runtime, scene->pbrPackets);
    scene->pbrPackets = packets;
    scene->pbrCapacity = scene->memberCapacity;
    return BL_OK;
}

bl_Status l_preparePbrScene(bl_Runtime* runtime, L_Scene* scene)
{
    bool hasPbr = false;
    for (size_t i = 0; i < scene->memberCount; ++i)
    {
        L_Node* node = scene->members[i];
        if (node->record.kind == L_MESH && !node->record.disposed)
        {
            bl_Material identity = l_meshMaterial((L_Mesh*)node);
            L_Record* material = l_peek(runtime, identity._id);
            hasPbr |= material && material->kind == L_PBR;
        }
    }
    if (!hasPbr)
    {
        return BL_OK;
    }
    if (!scene->environment || scene->environment->record.disposed)
    {
        return BL_NOT_READY;
    }
    L_TRY(reservePackets(runtime, scene));
    for (size_t i = 0; i < scene->memberCount; ++i)
    {
        L_Node* node = scene->members[i];
        if (node->record.kind != L_MESH || node->record.disposed)
        {
            continue;
        }
        L_Mesh* mesh = (L_Mesh*)node;
        bl_Material identity = l_meshMaterial(mesh);
        L_Material* material = (L_Material*)l_peek(runtime, identity._id);
        if (!material || material->record.kind != L_PBR)
        {
            cleanupPacket(runtime, scene->pbrPackets + i);
            continue;
        }
        L_PbrPacket* packet = scene->pbrPackets + i;
        bool colors = (mesh->geometry.attributeMask & (1u << BL_ATTRIBUTE_COLOR)) != 0;
        bool flatNormal = mesh->assetFlatNormal;
        bool tangents = !flatNormal && !L_NULL(material->pbrNormalTexture) &&
                        (mesh->geometry.attributeMask & (1u << BL_ATTRIBUTE_TANGENT));
        if (packet->material == material && packet->instances == mesh->hasInstances &&
            packet->tangents == tangents && packet->colors == colors &&
            packet->flatNormal == flatNormal)
        {
            if (packet->environment != scene->environment)
            {
                l_unpin(&packet->environment->record);
                packet->environment = scene->environment;
                l_pin(&packet->environment->record);
            }
            continue;
        }
        L_Text vertex = {};
        L_Text fragment = {};
        bl_Status status = l_pbrSources(runtime, mesh->hasInstances, &vertex, &fragment,
                                        material->pbrInfo.doubleSided, material->pbrBaseImage,
                                        material->pbrBaseUv1, material->pbrOrmUv1, material,
                                        tangents, colors, flatNormal);
        const bl_InstanceAttributeDeclaration instances[4] = {{{"world0", 6}, 3, 0, 0, 64},
                                                              {{"world1", 6}, 4, 1, 16, 64},
                                                              {{"world2", 6}, 5, 2, 32, 64},
                                                              {{"world3", 6}, 6, 3, 48, 64}};
        L_NativePipeline pipeline = {};
        pipeline.program.idx = BL_INVALID_BGFX_HANDLE;
        if (status == BL_OK)
        {
            status = l_compileNativePipeline(runtime, scene->engine, {vertex.data, vertex.length},
                                             {fragment.data, fragment.length},
                                             mesh->hasInstances ? instances : NULL,
                                             mesh->hasInstances ? 4 : 0, &pipeline);
        }
        l_free(runtime, vertex.data);
        l_free(runtime, fragment.data);
        if (status != BL_OK)
        {
            return status;
        }
        if (packet->material)
        {
            l_freeNativePipeline(runtime, &packet->pipeline);
            l_unpin(&packet->material->record);
            l_unpin(&packet->environment->record);
        }
        packet->pipeline = pipeline;
        packet->material = material;
        packet->environment = scene->environment;
        packet->instances = mesh->hasInstances;
        packet->tangents = tangents;
        packet->colors = colors;
        packet->flatNormal = flatNormal;
        l_pin(&material->record);
        l_pin(&packet->environment->record);
    }
    return BL_OK;
}

static bl_Status instanceBuffer(bl_Runtime* runtime, L_Mesh* mesh)
{
    if (!mesh->hasInstances || !mesh->instanceCount)
    {
        return BL_OK;
    }
    if (mesh->instanceBufferIndex == BL_INVALID_BGFX_HANDLE)
    {
        bgfx::VertexLayout layout;
        layout.begin()
            .add(bgfx::Attrib::TexCoord7, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord6, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord5, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord4, 4, bgfx::AttribType::Float)
            .end();
        mesh->instanceBufferIndex =
            bgfx::createDynamicVertexBuffer((uint32_t)mesh->instanceCount, layout,
                                            BGFX_BUFFER_ALLOW_RESIZE)
                .idx;
        if (mesh->instanceBufferIndex == BL_INVALID_BGFX_HANDLE)
        {
            return BL_OUT_OF_MEMORY;
        }
    }
    if (mesh->instanceVersion != mesh->instanceGpuVersion)
    {
        bgfx::update(bgfx::DynamicVertexBufferHandle{mesh->instanceBufferIndex}, 0,
                     bgfx::copy(mesh->instanceMatrices, (uint32_t)(mesh->instanceCount * 64)));
        mesh->instanceGpuVersion = mesh->instanceVersion;
    }
    (void)runtime;
    return BL_OK;
}

bl_Status l_drawPbr(bl_Runtime* runtime, L_Scene* scene, L_Mesh* mesh, L_Material* material,
                    const bl_Mat4* view, const bl_Mat4* projection, bl_Vec3 eye, uint16_t viewId)
{
    if (mesh->hasInstances && !mesh->instanceCount)
    {
        return BL_OK;
    }
    L_PbrPacket* packet = NULL;
    for (size_t i = 0; i < scene->memberCount; ++i)
    {
        if (scene->members[i] == &mesh->node && scene->pbrPackets[i].material == material)
        {
            packet = scene->pbrPackets + i;
            break;
        }
    }
    if (!packet)
    {
        return BL_NOT_READY;
    }
    float sceneData[92] = {};
    bl_Mat4 viewProjection;
    l_multiply(projection, view, &viewProjection, scene->engine->highPrecision);
    for (unsigned i = 0; i < 16; ++i)
    {
        sceneData[i] = (float)viewProjection.values[i];
        sceneData[16 + i] = (float)view->values[i];
    }
    sceneData[32] = (float)eye.x;
    sceneData[33] = (float)eye.y;
    sceneData[34] = (float)eye.z;
    sceneData[35] = 1;
    memcpy(sceneData + 40, packet->environment->harmonics, 36 * sizeof(float));
    sceneData[76] = (float)scene->exposure;
    sceneData[77] = (float)scene->contrast;
    sceneData[78] = 0.8f;
    float meshData[36] = {};
    for (unsigned i = 0; i < 16; ++i)
    {
        meshData[i] = (float)mesh->node.world.values[i];
    }
    float materialData[16] = {1, 1, 0.04f, 1, 1, 1, 1, 1};
    const bl_PbrMaterialInfo* info = &material->pbrInfo;
    materialData[0] = (float)info->environmentIntensity;
    materialData[1] = (float)info->directIntensity;
    materialData[2] = (float)info->reflectance;
    materialData[3] = (float)info->alpha;
    materialData[4] = (float)info->metallicFactor;
    materialData[5] = (float)info->roughnessFactor;
    materialData[6] = (float)material->pbrNormalScale;
    materialData[7] = (float)material->pbrAlphaCutoff;
    materialData[8] = (float)info->baseColorFactor.r;
    materialData[9] = (float)info->baseColorFactor.g;
    materialData[10] = (float)info->baseColorFactor.b;
    materialData[11] = (float)info->baseColorFactor.a;
    materialData[12] = (float)material->pbrEmissiveFactor.x;
    materialData[13] = (float)material->pbrEmissiveFactor.y;
    materialData[14] = (float)material->pbrEmissiveFactor.z;
    materialData[15] = (float)info->occlusionStrength;
    bl_Bytes blocks[3] = {};
    if (packet->pipeline.blockCount != 3)
    {
        return BL_SHADER_ERROR;
    }
    for (size_t i = 0; i < 3; ++i)
    {
        const L_PipelineBlock* block = packet->pipeline.blocks + i;
        if (l_name(block->name, "scene"))
        {
            blocks[i] = {(const uint8_t*)sceneData, sizeof(sceneData)};
        }
        else if (l_name(block->name, "mesh"))
        {
            blocks[i] = {(const uint8_t*)meshData, sizeof(meshData)};
        }
        else if (l_name(block->name, "material"))
        {
            size_t floats = material->pbrExtra ? 16 : material->pbrBaseImage ? 12 : 8;
            blocks[i] = {(const uint8_t*)materialData, sizeof(float) * floats};
        }
        else
        {
            return BL_SHADER_ERROR;
        }
    }
    L_TRY(l_bindNativeBlocks(runtime, &packet->pipeline, blocks, 3));
    for (size_t i = 0; i < packet->pipeline.textureCount; ++i)
    {
        L_PipelineTexture* binding = packet->pipeline.textures + i;
        bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
        uint8_t mipCount = 1;
        uint8_t maxLod = 128;
        uint16_t layerCount = 1;
        uint32_t flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP;
        if (l_name(binding->name, "iblTexture"))
        {
            L_Cube* cube = (L_Cube*)l_peek(runtime, packet->environment->info.specularCube._id);
            texture = cube->texture;
            mipCount = (uint8_t)cube->info.mipCount;
            layerCount = 6;
        }
        else
        {
            bl_Texture2D handle =
                l_name(binding->name, "baseColorTexture")   ? info->baseColorTexture
                : l_name(binding->name, "ormTexture")       ? info->ormTexture
                : l_name(binding->name, "normalTexture")    ? material->pbrNormalTexture
                : l_name(binding->name, "occlusionTexture") ? material->pbrOcclusionTexture
                : l_name(binding->name, "emissiveTexture")  ? material->pbrEmissiveTexture
                : l_name(binding->name, "brdfLUT")          ? packet->environment->info.brdfLut
                                                            : bl_Texture2D{};
            L_Record* record = l_peek(runtime, handle._id);
            if (!record || record->disposed || record->kind != L_TEXTURE)
            {
                return BL_NOT_READY;
            }
            L_Texture* resource = (L_Texture*)record;
            texture.idx = resource->handleIndex;
            mipCount = resource->mipCount ? resource->mipCount : 1;
            if (l_name(binding->name, "brdfLUT"))
            {
                flags |= BGFX_SAMPLER_MIP_POINT;
            }
            else
            {
                bool authored = l_name(binding->name, "baseColorTexture") ? material->pbrBaseImage
                                : l_name(binding->name, "ormTexture")     ? material->pbrOrmImage
                                                                          : true;
                flags = authored ? (uint32_t)resource->flags
                                 : BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC;
                if (authored)
                {
                    maxLod = resource->samplerMaxLod;
                }
            }
        }
        bgfx::setTexture(binding->stage, binding->uniform, texture, 0, layerCount, 0, mipCount,
                         flags, 0, maxLod);
    }
    L_TRY(instanceBuffer(runtime, mesh));
    bgfx::setVertexBuffer(0, mesh->geometry.vertexBuffer, 0, (uint32_t)mesh->geometry.vertices);
    bgfx::setIndexBuffer(mesh->geometry.indexBuffer, 0, (uint32_t)mesh->geometry.indexCount);
    if (mesh->hasInstances)
    {
        bgfx::setInstanceDataBuffer(bgfx::DynamicVertexBufferHandle{mesh->instanceBufferIndex}, 0,
                                    (uint32_t)mesh->instanceCount);
    }
    double determinant =
        (double)meshData[0] * (meshData[5] * meshData[10] - meshData[6] * meshData[9]) -
        (double)meshData[4] * (meshData[1] * meshData[10] - meshData[2] * meshData[9]) +
        (double)meshData[8] * (meshData[1] * meshData[6] - meshData[2] * meshData[5]);
    bgfx::setState(l_pbrDrawState(material, determinant,
                                  scene->engine->native.target.depthFormat != BL_DEPTH_NONE));
    bgfx::submit(viewId, packet->pipeline.program);
    ++scene->engine->stats.drawCallCount;
    return BL_OK;
}

uint64_t l_pbrDrawState(const L_Material* material, double determinant, bool depth)
{
    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA;
    if (depth)
    {
        state |= BGFX_STATE_DEPTH_TEST_GEQUAL;
        if (material->depthWrite)
        {
            state |= BGFX_STATE_WRITE_Z;
        }
    }
    if (material->culling)
    {
        state |= determinant > 0 ? BGFX_STATE_CULL_CCW : BGFX_STATE_CULL_CW;
    }
    if (material->blending)
    {
        state |= BGFX_STATE_BLEND_FUNC_SEPARATE(
            BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA, BGFX_STATE_BLEND_ONE,
            BGFX_STATE_BLEND_INV_SRC_ALPHA);
    }
    return state;
}
