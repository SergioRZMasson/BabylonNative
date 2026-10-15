#include "AssetMotionInternal.h"

static void* motionArray(bl_Runtime* runtime, size_t count, size_t size)
{
    size_t bytes;
    if (!l_size(count, size, &bytes))
    {
        return NULL;
    }
    return l_alloc(runtime, bytes);
}

static void cleanupGroup(bl_Runtime* runtime, L_Record* record)
{
    L_AssetAnimation* group = (L_AssetAnimation*)record;
    for (size_t i = 0; i < group->samplerCount; ++i)
    {
        if (group->samplers)
        {
            l_free(runtime, group->samplers[i].times);
            l_free(runtime, group->samplers[i].values);
        }
    }
    l_free(runtime, group->samplers);
    l_free(runtime, group->channels);
    l_free(runtime, (void*)group->info.name.data);
    group->samplers = NULL;
    group->channels = NULL;
    group->samplerCount = 0;
    group->info.name = {};
    record->disposed = true;
}

void l_cleanupAssetMotion(L_Asset* asset)
{
    L_AssetMotion* motion = asset->motion;
    if (!motion)
    {
        return;
    }
    bl_Runtime* runtime = asset->engine->runtime;
    for (size_t i = 0; i < runtime->count; ++i)
    {
        L_Record* record = runtime->records[i];
        if (record && !record->disposed && record->kind == L_SCENE)
        {
            L_Callback** cursor = &((L_Scene*)record)->before;
            while (*cursor)
            {
                L_Callback* callback = *cursor;
                if (callback->asset == asset)
                {
                    *cursor = callback->next;
                    l_free(runtime, callback);
                }
                else
                {
                    cursor = &callback->next;
                }
            }
        }
    }
    for (size_t i = 0; i < motion->groupCount; ++i)
    {
        L_Record* record = motion->groups ? l_peek(runtime, motion->groups[i]._id) : NULL;
        if (record)
        {
            if (!record->disposed)
            {
                cleanupGroup(runtime, record);
            }
            l_unpin(record);
        }
    }
    while (motion->deformations)
    {
        L_AssetDeformation* d = motion->deformations;
        motion->deformations = d->next;
        l_freeAssetDeformation(runtime, d);
    }
    for (size_t i = 0; i < motion->skinCount; ++i)
    {
        if (motion->skins)
        {
            l_free(runtime, motion->skins[i].joints);
            l_free(runtime, motion->skins[i].inverseBind);
            l_free(runtime, motion->skins[i].palette);
        }
    }
    l_free(runtime, motion->skins);
    l_free(runtime, motion->groups);
    l_free(runtime, motion->nodes);
    l_free(runtime, motion->parents);
    l_free(runtime, motion->order);
    l_free(runtime, motion->rest);
    l_free(runtime, motion->nextPose);
    l_free(runtime, motion->locals);
    l_free(runtime, motion->worlds);
    l_free(runtime, motion->targets);
    l_free(runtime, motion->matrixLocked);
    l_free(runtime, motion);
    asset->motion = NULL;
}

static bl_Status copySampler(bl_Runtime* runtime, const bl_ParsedGltf6* full,
                             const bl_AssetAnimationSampler* source, L_AssetSampler* target)
{
    const bl_ParsedGltf* data = &full->document5.document4.document3.document;
    L_AssetAccessorReader input;
    L_AssetAccessorReader output;
    L_TRY(l_assetAccessorReader(data, source->input, &full->document5.document4, &input));
    L_TRY(l_assetAccessorReader(data, source->output, &full->document5.document4, &output));
    target->count = input.accessor->count;
    target->components = output.rows * output.columns;
    target->interpolation = l_assetInterpolation(source);
    target->times = (float*)motionArray(runtime, target->count, sizeof(float));
    size_t components;
    if (!l_size(output.accessor->count, target->components, &components))
    {
        return BL_INVALID_ARGUMENT;
    }
    target->values = (float*)motionArray(runtime, components, sizeof(float));
    if (!target->times || !target->values)
    {
        return BL_OUT_OF_MEMORY;
    }
    for (size_t i = 0; i < target->count; ++i)
    {
        double value;
        L_TRY(l_assetAccessorElement(&input, i, &value));
        target->times[i] = (float)value;
    }
    for (size_t i = 0; i < output.accessor->count; ++i)
    {
        double values[16];
        L_TRY(l_assetAccessorElement(&output, i, values));
        for (size_t c = 0; c < target->components; ++c)
        {
            target->values[i * target->components + c] = (float)values[c];
        }
    }
    return BL_OK;
}

bl_Status l_initAssetMotion(L_Asset* asset, const bl_ParsedGltf6* full)
{
    const bl_ParsedGltf* data = &full->document5.document4.document3.document;
    if (!data->skinCount && !data->animationCount)
    {
        return BL_OK;
    }
    bl_Runtime* runtime = asset->engine->runtime;
    L_AssetMotion* motion = (L_AssetMotion*)l_alloc(runtime, sizeof(*motion));
    if (!motion)
    {
        return BL_OUT_OF_MEMORY;
    }
    asset->motion = motion;
    motion->nodeCount = data->nodeCount;
    motion->skinCount = data->skinCount;
    motion->groupCount = data->animationCount;
    motion->nodes = (L_Node**)motionArray(runtime, data->nodeCount, sizeof(L_Node*));
    motion->parents = (int32_t*)motionArray(runtime, data->nodeCount, sizeof(int32_t));
    motion->order = (uint32_t*)motionArray(runtime, data->nodeCount, sizeof(uint32_t));
    motion->rest = (bl_TransformNodeOptions*)motionArray(runtime, data->nodeCount,
                                                         sizeof(bl_TransformNodeOptions));
    motion->nextPose = (bl_TransformNodeOptions*)motionArray(runtime, data->nodeCount,
                                                             sizeof(bl_TransformNodeOptions));
    motion->locals = (bl_Mat4*)motionArray(runtime, data->nodeCount, sizeof(bl_Mat4));
    motion->worlds = (bl_Mat4*)motionArray(runtime, data->nodeCount, sizeof(bl_Mat4));
    motion->targets = (uint8_t*)motionArray(runtime, data->nodeCount, 1);
    motion->matrixLocked = (uint8_t*)motionArray(runtime, data->nodeCount, 1);
    motion->skins =
        (L_AssetSkinData*)motionArray(runtime, data->skinCount, sizeof(L_AssetSkinData));
    motion->groups =
        (bl_AnimationGroup*)motionArray(runtime, data->animationCount, sizeof(bl_AnimationGroup));
    if ((data->nodeCount && (!motion->nodes || !motion->parents || !motion->order ||
                             !motion->rest || !motion->nextPose || !motion->locals ||
                             !motion->worlds || !motion->targets || !motion->matrixLocked)) ||
        (data->skinCount && !motion->skins) || (data->animationCount && !motion->groups))
    {
        return BL_OUT_OF_MEMORY;
    }
    for (size_t i = 0; i < data->nodeCount; ++i)
    {
        motion->parents[i] = -1;
        L_TRY(l_composeAssetMatrix(data->nodes + i, motion->rest + i));
        L_TRY(l_normalizeAssetQuaternion(&motion->rest[i].rotationQuaternion));
        if (data->nodes[i].hasMatrix)
        {
            motion->locals[i] = data->nodes[i].matrix;
            motion->matrixLocked[i] = true;
        }
    }
    for (size_t i = 0; i < data->nodeCount; ++i)
    {
        for (size_t j = 0; j < data->nodes[i].childCount; ++j)
        {
            motion->parents[data->nodes[i].children[j]] = (int32_t)i;
        }
    }
    size_t count = 0;
    for (size_t i = 0; i < data->nodeCount; ++i)
    {
        if (motion->parents[i] < 0)
        {
            motion->order[count++] = (uint32_t)i;
        }
    }
    for (size_t i = 0; i < count; ++i)
    {
        const bl_AssetNode* node = data->nodes + motion->order[i];
        for (size_t j = 0; j < node->childCount; ++j)
        {
            motion->order[count++] = node->children[j];
        }
    }
    for (size_t i = 0; i < data->skinCount; ++i)
    {
        const bl_AssetSkin* source = full->skins + i;
        L_AssetSkinData* target = motion->skins + i;
        target->count = source->jointCount;
        target->joints = (uint32_t*)motionArray(runtime, target->count, sizeof(uint32_t));
        target->inverseBind = (float*)motionArray(runtime, target->count, 16 * sizeof(float));
        target->palette = (float*)motionArray(runtime, target->count, 16 * sizeof(float));
        if (!target->joints || !target->inverseBind || !target->palette)
        {
            return BL_OUT_OF_MEMORY;
        }
        memcpy(target->joints, source->joints, target->count * sizeof(uint32_t));
        L_AssetAccessorReader reader = {};
        if (source->inverseBindMatrices.present)
        {
            L_TRY(l_assetAccessorReader(data, source->inverseBindMatrices.value,
                                        &full->document5.document4, &reader));
        }
        for (size_t j = 0; j < target->count; ++j)
        {
            double matrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            if (source->inverseBindMatrices.present)
            {
                L_TRY(l_assetAccessorElement(&reader, j, matrix));
            }
            for (size_t c = 0; c < 16; ++c)
            {
                target->inverseBind[j * 16 + c] = (float)matrix[c];
            }
        }
    }
    for (size_t i = 0; i < data->animationCount; ++i)
    {
        L_Record* record;
        L_TRY(l_record(runtime, sizeof(L_AssetAnimation), L_ANIMATION, 44, cleanupGroup, &record));
        l_pin(record);
        motion->groups[i] = {runtime, record->id};
        L_AssetAnimation* group = (L_AssetAnimation*)record;
        const bl_AssetAnimation* source = full->animations + i;
        group->asset = asset;
        group->samplerCount = source->samplerCount;
        group->channelCount = source->channelCount;
        group->info.frameRate = 60;
        group->info.speedRatio = 1;
        group->info.loopAnimation = true;
        group->info.isPlaying = i == 0;
        group->info.isStopped = i != 0;
        char fallback[64];
        snprintf(fallback, sizeof(fallback), "animation_%zu", i);
        group->info.name = l_copyString(runtime, source->hasName && source->name.length
                                                     ? source->name
                                                     : bl_String{fallback, strlen(fallback)});
        group->samplers =
            (L_AssetSampler*)motionArray(runtime, source->samplerCount, sizeof(L_AssetSampler));
        group->channels =
            (L_AssetChannel*)motionArray(runtime, source->channelCount, sizeof(L_AssetChannel));
        if (!group->info.name.data || !group->samplers || !group->channels)
        {
            return BL_OUT_OF_MEMORY;
        }
        for (size_t j = 0; j < source->samplerCount; ++j)
        {
            L_TRY(copySampler(runtime, full, source->samplers + j, group->samplers + j));
            const L_AssetSampler* sampler = group->samplers + j;
            double duration = sampler->times[sampler->count - 1];
            group->info.duration =
                duration > group->info.duration ? duration : group->info.duration;
        }
        for (size_t j = 0; j < source->channelCount; ++j)
        {
            const bl_AssetAnimationChannel* channel = source->channels + j;
            group->channels[j] = {channel->sampler,
                                  channel->node.present ? channel->node.value : UINT32_MAX,
                                  l_assetAnimationPath(channel->path)};
            if (channel->node.present)
            {
                motion->targets[channel->node.value] |= (uint8_t)(1u << group->channels[j].path);
            }
        }
    }
    return BL_OK;
}

bl_Status l_finishAssetMotion(L_Asset* asset)
{
    if (!asset->motion)
    {
        return BL_OK;
    }
    L_AssetMotion* motion = asset->motion;
    for (size_t i = 0; i < motion->groupCount; ++i)
    {
        L_AssetAnimation* group =
            (L_AssetAnimation*)l_peek(asset->engine->runtime, motion->groups[i]._id);
        for (size_t j = 0; j < group->channelCount; ++j)
        {
            uint32_t node = group->channels[j].node;
            group->info.targetedAnimationCount += node != UINT32_MAX && motion->nodes[node];
        }
    }
    L_AssetAnimation* first = motion->groupCount ? (L_AssetAnimation*)l_peek(asset->engine->runtime,
                                                                             motion->groups[0]._id)
                                                 : NULL;
    L_TRY(l_evaluateAssetMotion(asset, first, 0, false));
    if (motion->skinCount)
    {
        for (size_t i = 0; i < asset->nodeCount; ++i)
        {
            L_Node* node = (L_Node*)l_peek(asset->engine->runtime, asset->nodes[i]._id);
            node->assetParentLocked = asset->nodes[i]._id != asset->root._id;
            node->assetChildrenLocked = true;
        }
    }
    return BL_OK;
}

bl_Status l_addAssetMotionOwner(L_Asset* asset, L_Scene* scene)
{
    if (!asset->motion)
    {
        return BL_OK;
    }
    L_Callback** tail = &scene->before;
    while (*tail)
    {
        if ((*tail)->asset == asset)
        {
            ++(*tail)->assetOwners;
            return BL_OK;
        }
        tail = &(*tail)->next;
    }
    L_Callback* callback = (L_Callback*)l_alloc(scene->engine->runtime, sizeof(*callback));
    if (!callback)
    {
        return BL_OUT_OF_MEMORY;
    }
    callback->asset = asset;
    callback->assetOwners = 1;
    *tail = callback;
    return BL_OK;
}

void l_removeAssetMotionOwner(L_Asset* asset, L_Scene* scene)
{
    L_Callback** cursor = &scene->before;
    while (*cursor)
    {
        L_Callback* callback = *cursor;
        if (callback->asset == asset)
        {
            if (!--callback->assetOwners)
            {
                *cursor = callback->next;
                l_free(scene->engine->runtime, callback);
            }
            return;
        }
        cursor = &callback->next;
    }
}
