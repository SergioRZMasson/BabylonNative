#include "LiteInternal.h"
#include "HemisphericLightInternal.h"

static void standardGraphicsCleanup(bl_Runtime* r, L_Material* owner)
{
    for (unsigned i = 0; i < 2; ++i)
    {
        L_Material* pipeline = owner->standardVariants[i];
        if (pipeline)
        {
            l_materialCleanup(r, &pipeline->record);
            l_free(r, pipeline);
            owner->standardVariants[i] = NULL;
        }
    }
    owner->engine = NULL;
    owner->graphicsCleanup = NULL;
}

bl_Status l_prepareStandard(bl_Runtime* r, L_Engine* engine, L_Material* owner,
                            bool disableLighting, L_Material** out)
{
    if (owner->engine && owner->engine != engine)
    {
        return BL_WRONG_ENGINE;
    }
    unsigned index = disableLighting ? 1 : 0;
    L_Material* pipeline = owner->standardVariants[index];
    if (!pipeline)
    {
        pipeline = (L_Material*)l_alloc(r, sizeof(*pipeline));
        if (!pipeline)
        {
            return BL_OUT_OF_MEMORY;
        }
        bl_Status status = l_standardSources(r, pipeline, disableLighting);
        if (status == BL_OK)
        {
            status = l_prepareMaterial(r, engine, pipeline);
        }
        if (status != BL_OK)
        {
            l_materialCleanup(r, &pipeline->record);
            l_free(r, pipeline);
            return status;
        }
        owner->standardVariants[index] = pipeline;
        owner->graphicsCleanup = standardGraphicsCleanup;
        owner->engine = engine;
    }
    *out = pipeline;
    return BL_OK;
}

static bl_Status reservePackets(bl_Runtime* r, L_Scene* scene)
{
    if (scene->memberCount <= scene->standardCapacity)
    {
        return BL_OK;
    }
    size_t capacity = scene->memberCapacity;
    if (capacity < scene->memberCount)
    {
        capacity = scene->memberCount;
    }
    size_t bytes;
    if (!l_size(capacity, sizeof(L_StandardPacket), &bytes))
    {
        return BL_OUT_OF_MEMORY;
    }
    L_StandardPacket* packets = (L_StandardPacket*)l_alloc(r, bytes);
    if (!packets)
    {
        return BL_OUT_OF_MEMORY;
    }
    if (scene->standardCapacity)
    {
        memcpy(packets, scene->standardPackets,
               scene->standardCapacity * sizeof(*scene->standardPackets));
    }
    l_free(r, scene->standardPackets);
    scene->standardPackets = packets;
    scene->standardCapacity = capacity;
    return BL_OK;
}

bl_Status l_prepareStandardPackets(bl_Runtime* r, L_Scene* scene, bool force, uint64_t materialId)
{
    if (!scene->standardMemberCount)
    {
        return BL_OK;
    }
    L_TRY(reservePackets(r, scene));
    // Validate/prepare every requested pipeline before committing packet snapshots.
    for (size_t i = 0; i < scene->memberCount; ++i)
    {
        L_Node* n = scene->members[i];
        if (n->record.disposed || n->record.kind != L_MESH)
        {
            continue;
        }
        L_Mesh* mesh = (L_Mesh*)n;
        bl_Material identity = l_meshMaterial(mesh);
        L_Material* material = (L_Material*)l_peek(r, identity._id);
        if (!material || material->record.disposed || material->record.kind != L_STANDARD ||
            (materialId && materialId != identity._id))
        {
            continue;
        }
        L_StandardPacket* packet = scene->standardPackets + i;
        if (!force && packet->meshId == n->record.id && packet->materialId == identity._id)
        {
            continue;
        }
        L_TRY(l_checkStandardGeometry(r, material, mesh));
        bool lighting = force || !material->snapshotValid
                            ? material->standardProperties.disableLighting
                            : material->standardSnapshot.disableLighting;
        L_Material* pipeline;
        L_TRY(l_prepareStandard(r, scene->engine, material, lighting, &pipeline));
    }
    for (size_t i = 0; i < scene->memberCount; ++i)
    {
        L_Node* n = scene->members[i];
        if (n->record.disposed || n->record.kind != L_MESH)
        {
            continue;
        }
        bl_Material identity = l_meshMaterial((L_Mesh*)n);
        L_Material* material = (L_Material*)l_peek(r, identity._id);
        if (!material || material->record.disposed || material->record.kind != L_STANDARD ||
            (materialId && materialId != identity._id))
        {
            continue;
        }
        L_StandardPacket* packet = scene->standardPackets + i;
        if (!force && packet->meshId == n->record.id && packet->materialId == identity._id)
        {
            continue;
        }
        if (force || !material->snapshotValid)
        {
            material->standardSnapshot = material->standardProperties;
            material->snapshotValid = true;
        }
        const bl_StandardMaterialProperties* features = &material->standardSnapshot;
        packet->meshId = n->record.id;
        packet->materialId = material->record.id;
        packet->uboVersion = material->uboVersion;
        packet->properties = material->standardProperties;
        packet->transparent = material->standardProperties.alpha < 1;
        packet->culling = features->backFaceCulling;
        packet->pipeline = material->standardVariants[features->disableLighting ? 1 : 0];
    }
    return BL_OK;
}

static bl_Status sceneLights(bl_Runtime* r, L_Scene* scene)
{
    size_t count = 0;
    uint64_t versions[16] = {};
    uint64_t identities[16] = {};
    L_HemisphericLight* lights[16] = {};
    for (size_t i = 0; i < scene->memberCount && count < 16; ++i)
    {
        L_Node* node = scene->members[i];
        if (node->record.disposed || node->record.kind != L_LIGHT)
        {
            continue;
        }
        L_TRY(l_world(r, node));
        L_HemisphericLight* light = (L_HemisphericLight*)node;
        identities[count] = node->record.id;
        versions[count] = light->dataVersion + node->version;
        lights[count++] = light;
    }
    if (scene->lightsReady && scene->packedListVersion == scene->lightListVersion &&
        scene->packedLightCount == count &&
        !memcmp(identities, scene->lightIdentities, sizeof(identities)) &&
        !memcmp(versions, scene->lightVersions, sizeof(versions)))
    {
        return BL_OK;
    }
    float data[260] = {};
    uint32_t packedCount = (uint32_t)count;
    memcpy(data, &packedCount, sizeof(packedCount));
    for (size_t i = 0; i < count; ++i)
    {
        L_TRY(l_writeHemisphericLight(r, lights[i], data + 4 + i * 16));
    }
    memcpy(scene->lightData, data, sizeof(data));
    memcpy(scene->lightIdentities, identities, sizeof(identities));
    memcpy(scene->lightVersions, versions, sizeof(versions));
    scene->packedListVersion = scene->lightListVersion;
    scene->packedLightCount = count;
    scene->lightsReady = true;
    return BL_OK;
}

static void matrixFloats(float* values, const bl_Mat4* matrix)
{
    for (unsigned i = 0; i < 16; ++i)
    {
        values[i] = (float)matrix->values[i];
    }
}

bl_Status l_standardValues(bl_Runtime* r, L_Engine* engine, L_Scene* scene,
                           L_StandardPacket* packet, L_Mesh* mesh, const bl_Mat4* view,
                           const bl_Mat4* projection, bl_Vec3 position)
{
    L_Material* owner = (L_Material*)l_peek(r, packet->materialId);
    if (!owner || owner->record.disposed)
    {
        return BL_DISPOSED;
    }
    if (packet->uboVersion != owner->uboVersion)
    {
        packet->properties = owner->standardProperties;
        packet->uboVersion = owner->uboVersion;
    }
    L_TRY(sceneLights(r, scene));
    float sceneData[92] = {};
    bl_Mat4 viewProjection;
    l_multiply(projection, view, &viewProjection, engine->highPrecision);
    matrixFloats(sceneData, &viewProjection);
    matrixFloats(sceneData + 16, view);
    sceneData[32] = (float)position.x;
    sceneData[33] = (float)position.y;
    sceneData[34] = (float)position.z;
    for (unsigned i = 0; i < 9; ++i)
    {
        sceneData[40 + i * 4] = 1;
        sceneData[41 + i * 4] = 1;
        sceneData[42 + i * 4] = 1;
    }
    sceneData[76] = 1;
    sceneData[77] = 1;
    sceneData[84] = 1;
    sceneData[85] = 1;
    sceneData[86] = 1;
    float meshData[36] = {};
    matrixFloats(meshData, &mesh->node.world);
    uint32_t count = (uint32_t)scene->packedLightCount;
    memcpy(meshData + 16, &count, sizeof(count));
    for (uint32_t i = 0; i < count; ++i)
    {
        memcpy(meshData + 20 + i, &i, sizeof(i));
    }
    const bl_StandardMaterialProperties* p = &packet->properties;
    float materialData[24] = {(float)p->diffuseColor.x,
                              (float)p->diffuseColor.y,
                              (float)p->diffuseColor.z,
                              (float)p->alpha,
                              (float)p->specularColor.x,
                              (float)p->specularColor.y,
                              (float)p->specularColor.z,
                              (float)p->specularPower,
                              (float)p->emissiveColor.x,
                              (float)p->emissiveColor.y,
                              (float)p->emissiveColor.z,
                              1,
                              (float)p->ambientColor.x,
                              (float)p->ambientColor.y,
                              (float)p->ambientColor.z,
                              1,
                              1,
                              1,
                              1,
                              0,
                              1,
                              1,
                              0,
                              0};
    const float* blocks[4] = {sceneData, scene->lightData, meshData, materialData};
    L_Material* pipeline = packet->pipeline;
    for (size_t i = 0; i < pipeline->uniformCount; ++i)
    {
        L_Uniform* uniform = pipeline->uniforms + i;
        memcpy(uniform->values, blocks[uniform->block] + uniform->offset / 4,
               uniformCounts[uniform->type] * sizeof(float));
    }
    return BL_OK;
}

bl_Status bl_rebuildMaterial(bl_SceneContext h, bl_Material identity,
                             const bl_RebuildMaterialOptions* options)
{
    L_GET(h, L_SCENE, L_Scene, scene);
    if (options && ((unsigned)options->rebuildViews > BL_BOOL_TRUE ||
                    (unsigned)options->rebuildFrameGraph > BL_BOOL_TRUE))
    {
        return BL_INVALID_ARGUMENT;
    }
    if (options && options->rebuildFrameGraph == BL_BOOL_TRUE)
    {
        return BL_UNSUPPORTED;
    }
    if (identity._runtime != h._runtime)
    {
        return BL_WRONG_RUNTIME;
    }
    L_Material* material;
    L_TRY(l_materialFamily(identity, &material));
    if (material->engine && material->engine != scene->engine)
    {
        return BL_WRONG_ENGINE;
    }
    if (!scene->registered)
    {
        return BL_OK;
    }
    if (material->record.kind == L_STANDARD)
    {
        return l_prepareStandardPackets(h._runtime, scene, true, identity._id);
    }
    return l_prepareMaterial(h._runtime, scene->engine, material);
}

bl_Status bl_rebuildSceneRenderables(bl_SceneContext h)
{
    L_GET(h, L_SCENE, L_Scene, scene);
    if (!scene->registered)
    {
        return BL_OK;
    }
    return l_prepareStandardPackets(h._runtime, scene, true, 0);
}
