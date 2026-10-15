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

bl_Status l_createAssetMotion(L_Asset* asset, size_t nodes, size_t skins, size_t groups)
{
    if (nodes > INT32_MAX || skins > UINT32_MAX || groups > UINT32_MAX)
    {
        return BL_INVALID_ARGUMENT;
    }
    bl_Runtime* runtime = asset->engine->runtime;
    L_AssetMotion* motion = (L_AssetMotion*)l_alloc(runtime, sizeof(*motion));
    if (!motion)
    {
        return BL_OUT_OF_MEMORY;
    }
    asset->motion = motion;
    motion->nodeCount = nodes;
    motion->skinCount = skins;
    motion->groupCount = groups;
    motion->nodes = (L_Node**)motionArray(runtime, nodes, sizeof(L_Node*));
    motion->parents = (int32_t*)motionArray(runtime, nodes, sizeof(int32_t));
    motion->order = (uint32_t*)motionArray(runtime, nodes, sizeof(uint32_t));
    motion->rest =
        (bl_TransformNodeOptions*)motionArray(runtime, nodes, sizeof(bl_TransformNodeOptions));
    motion->nextPose =
        (bl_TransformNodeOptions*)motionArray(runtime, nodes, sizeof(bl_TransformNodeOptions));
    motion->locals = (bl_Mat4*)motionArray(runtime, nodes, sizeof(bl_Mat4));
    motion->worlds = (bl_Mat4*)motionArray(runtime, nodes, sizeof(bl_Mat4));
    motion->targets = (uint8_t*)motionArray(runtime, nodes, 1);
    motion->matrixLocked = (uint8_t*)motionArray(runtime, nodes, 1);
    motion->skins = (L_AssetSkinData*)motionArray(runtime, skins, sizeof(L_AssetSkinData));
    motion->groups = (bl_AnimationGroup*)motionArray(runtime, groups, sizeof(bl_AnimationGroup));
    if ((nodes && (!motion->nodes || !motion->parents || !motion->order || !motion->rest ||
                   !motion->nextPose || !motion->locals || !motion->worlds || !motion->targets ||
                   !motion->matrixLocked)) ||
        (skins && !motion->skins) || (groups && !motion->groups))
    {
        return BL_OUT_OF_MEMORY;
    }
    return BL_OK;
}

bl_Status l_createAssetAnimation(L_Asset* asset, size_t index, bl_String name, size_t samplers,
                                 size_t channels, L_AssetAnimation** out)
{
    bl_Runtime* runtime = asset->engine->runtime;
    if (!l_string(name) || index >= asset->motion->groupCount || samplers > UINT32_MAX ||
        channels > UINT32_MAX)
    {
        return BL_INVALID_ARGUMENT;
    }
    L_Record* record;
    L_TRY(l_record(runtime, sizeof(L_AssetAnimation), L_ANIMATION, 44, cleanupGroup, &record));
    l_pin(record);
    asset->motion->groups[index] = {runtime, record->id};
    L_AssetAnimation* group = (L_AssetAnimation*)record;
    group->asset = asset;
    group->samplerCount = samplers;
    group->channelCount = channels;
    group->info.frameRate = 60;
    group->info.speedRatio = 1;
    group->info.loopAnimation = true;
    group->info.isPlaying = index == 0;
    group->info.isStopped = index != 0;
    group->info.name = l_copyString(runtime, name);
    group->samplers = (L_AssetSampler*)motionArray(runtime, samplers, sizeof(L_AssetSampler));
    group->channels = (L_AssetChannel*)motionArray(runtime, channels, sizeof(L_AssetChannel));
    if (!group->info.name.data || (samplers && !group->samplers) || (channels && !group->channels))
    {
        return BL_OUT_OF_MEMORY;
    }
    *out = group;
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
    // Source-node animation uses copied topology too, including containers without skins.
    for (size_t i = 0; i < asset->nodeCount; ++i)
    {
        L_Node* node = (L_Node*)l_peek(asset->engine->runtime, asset->nodes[i]._id);
        node->assetParentLocked = asset->nodes[i]._id != asset->root._id;
        node->assetChildrenLocked = true;
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
            bool ownsRoot = false;
            for (size_t i = 0; i < scene->memberCount; ++i)
            {
                ownsRoot |= scene->members[i]->record.id == asset->root._id;
            }
            (*tail)->assetOwners = ownsRoot ? (*tail)->assetOwners + 1 : 1;
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
            bool ownsRoot = false;
            for (size_t i = 0; i < scene->memberCount; ++i)
            {
                ownsRoot |= scene->members[i]->record.id == asset->root._id;
            }
            if (!ownsRoot || !--callback->assetOwners)
            {
                *cursor = callback->next;
                l_free(scene->engine->runtime, callback);
            }
            return;
        }
        cursor = &callback->next;
    }
}
