#include "AssetMotionInternal.h"

bl_Status l_normalizeAssetQuaternion(bl_Quat* q)
{
    double length = sqrt(q->x * q->x + q->y * q->y + q->z * q->z + q->w * q->w);
    if (!isfinite(length) || length == 0)
    {
        return BL_INVALID_ARGUMENT;
    }
    q->x /= length;
    q->y /= length;
    q->z /= length;
    q->w /= length;
    return BL_OK;
}

void l_composeAssetPose(const bl_TransformNodeOptions* pose, bl_Mat4* matrix)
{
    double x = pose->rotationQuaternion.x;
    double y = pose->rotationQuaternion.y;
    double z = pose->rotationQuaternion.z;
    double w = pose->rotationQuaternion.w;
    double sx = pose->scaling.x;
    double sy = pose->scaling.y;
    double sz = pose->scaling.z;
    *matrix = {{(float)((1 - 2 * (y * y + z * z)) * sx), (float)(2 * (x * y + w * z) * sx),
                (float)(2 * (x * z - w * y) * sx), 0, (float)(2 * (x * y - w * z) * sy),
                (float)((1 - 2 * (x * x + z * z)) * sy), (float)(2 * (y * z + w * x) * sy), 0,
                (float)(2 * (x * z + w * y) * sz), (float)(2 * (y * z - w * x) * sz),
                (float)((1 - 2 * (x * x + y * y)) * sz), 0, (float)pose->position.x,
                (float)pose->position.y, (float)pose->position.z, 1}};
}

bl_Status l_sampleAssetAnimation(const L_AssetSampler* sampler, double time, bool quaternion,
                                 float* result)
{
    size_t stride = sampler->components;
    size_t count = sampler->count;
    size_t multiplier = sampler->interpolation == 2 ? 3 : 1;
    size_t first = multiplier == 3 ? stride : 0;
    if (count == 1 || time <= sampler->times[0])
    {
        memcpy(result, sampler->values + first, stride * sizeof(float));
    }
    else if (time >= sampler->times[count - 1])
    {
        memcpy(result, sampler->values + (count - 1) * multiplier * stride + first,
               stride * sizeof(float));
    }
    else
    {
        size_t lo = 0;
        size_t hi = count - 1;
        while (lo + 1 < hi)
        {
            size_t middle = lo + (hi - lo) / 2;
            if (sampler->times[middle] <= time)
            {
                lo = middle;
            }
            else
            {
                hi = middle;
            }
        }
        double duration = (double)sampler->times[hi] - sampler->times[lo];
        double f = (time - sampler->times[lo]) / duration;
        const float* a = sampler->values + lo * multiplier * stride;
        const float* b = sampler->values + hi * multiplier * stride;
        if (sampler->interpolation == 1)
        {
            memcpy(result, a, stride * sizeof(float));
        }
        else if (sampler->interpolation == 2)
        {
            double f2 = f * f;
            double f3 = f2 * f;
            for (size_t c = 0; c < stride; ++c)
            {
                result[c] =
                    (float)((2 * f3 - 3 * f2 + 1) * a[stride + c] +
                            (f3 - 2 * f2 + f) * duration * a[2 * stride + c] +
                            (-2 * f3 + 3 * f2) * b[stride + c] + (f3 - f2) * duration * b[c]);
            }
        }
        else if (quaternion)
        {
            bl_Quat qa = {a[0], a[1], a[2], a[3]};
            bl_Quat qb = {b[0], b[1], b[2], b[3]};
            L_TRY(l_normalizeAssetQuaternion(&qa));
            L_TRY(l_normalizeAssetQuaternion(&qb));
            double aa[4] = {qa.x, qa.y, qa.z, qa.w};
            double bb[4] = {qb.x, qb.y, qb.z, qb.w};
            double dot = 0;
            for (size_t c = 0; c < 4; ++c)
            {
                dot += aa[c] * bb[c];
            }
            double sign = dot < 0 ? -1 : 1;
            dot = fabs(dot);
            double wa = 1 - f;
            double wb = f;
            if (dot <= .9995)
            {
                double theta = acos(dot > 1 ? 1 : dot);
                wa = sin((1 - f) * theta) / sin(theta);
                wb = sin(f * theta) / sin(theta);
            }
            for (size_t c = 0; c < 4; ++c)
            {
                result[c] = (float)(wa * aa[c] + wb * sign * bb[c]);
            }
        }
        else
        {
            for (size_t c = 0; c < stride; ++c)
            {
                result[c] = (float)(a[c] + f * ((double)b[c] - a[c]));
            }
        }
    }
    if (quaternion)
    {
        bl_Quat q = {result[0], result[1], result[2], result[3]};
        L_TRY(l_normalizeAssetQuaternion(&q));
        result[0] = (float)q.x;
        result[1] = (float)q.y;
        result[2] = (float)q.z;
        result[3] = (float)q.w;
    }
    for (size_t c = 0; c < stride; ++c)
    {
        if (!isfinite(result[c]))
        {
            return BL_INVALID_ARGUMENT;
        }
    }
    return BL_OK;
}

bl_Status l_evaluateAssetMotion(L_Asset* asset, L_AssetAnimation* group, double time, bool reset)
{
    L_AssetMotion* motion = asset->motion;
    for (size_t i = 0; i < motion->nodeCount; ++i)
    {
        L_Node* node = motion->nodes[i];
        bl_TransformNodeOptions* pose = motion->nextPose + i;
        *pose = node && !node->record.disposed
                    ? bl_TransformNodeOptions{node->position, node->quaternion, node->scale}
                    : motion->rest[i];
        if (reset)
        {
            if (motion->targets[i] & 1)
            {
                pose->position = motion->rest[i].position;
            }
            if (motion->targets[i] & 2)
            {
                pose->rotationQuaternion = motion->rest[i].rotationQuaternion;
            }
            if (motion->targets[i] & 4)
            {
                pose->scaling = motion->rest[i].scaling;
            }
        }
    }
    if (group)
    {
        for (size_t i = 0; i < group->channelCount; ++i)
        {
            const L_AssetChannel* channel = group->channels + i;
            if (channel->node == UINT32_MAX || !motion->nodes[channel->node] ||
                motion->nodes[channel->node]->record.disposed)
            {
                continue;
            }
            float sample[4];
            L_TRY(l_sampleAssetAnimation(group->samplers + channel->sampler, time,
                                         channel->path == 1, sample));
            bl_TransformNodeOptions* pose = motion->nextPose + channel->node;
            if (channel->path == 0)
            {
                pose->position = {sample[0], sample[1], sample[2]};
            }
            else if (channel->path == 1)
            {
                pose->rotationQuaternion = {sample[0], sample[1], sample[2], sample[3]};
            }
            else
            {
                pose->scaling = {sample[0], sample[1], sample[2]};
            }
        }
    }
    for (size_t n = 0; n < motion->nodeCount; ++n)
    {
        size_t i = motion->order[n];
        if (!motion->matrixLocked[i])
        {
            L_TRY(l_normalizeAssetQuaternion(&motion->nextPose[i].rotationQuaternion));
            l_composeAssetPose(motion->nextPose + i, motion->locals + i);
        }
        int32_t parent = motion->parents[i];
        if (parent < 0)
        {
            motion->worlds[i] = motion->locals[i];
        }
        else
        {
            l_multiply(motion->worlds + parent, motion->locals + i, motion->worlds + i, false);
        }
        for (size_t c = 0; c < 16; ++c)
        {
            if (!isfinite(motion->worlds[i].values[c]))
            {
                return BL_INVALID_ARGUMENT;
            }
        }
    }
    L_TRY(l_prepareAssetDeformation(asset));
    l_commitAssetDeformation(asset);
    for (size_t i = 0; i < motion->nodeCount; ++i)
    {
        L_Node* node = motion->nodes[i];
        const bl_TransformNodeOptions* pose = motion->nextPose + i;
        if (node && !node->record.disposed)
        {
            if (memcmp(&node->position, &pose->position, sizeof(bl_Vec3)) ||
                memcmp(&node->scale, &pose->scaling, sizeof(bl_Vec3)) ||
                memcmp(&node->quaternion, &pose->rotationQuaternion, sizeof(bl_Quat)))
            {
                node->position = pose->position;
                node->scale = pose->scaling;
                node->quaternion = pose->rotationQuaternion;
                node->eulerValid = false;
                node->dirty |= !node->localMatrixLocked;
            }
        }
    }
    return BL_OK;
}

static double sampleTime(const bl_AnimationGroupInfo* info, double time)
{
    if (info->duration <= 0)
    {
        return 0;
    }
    if (info->loopAnimation)
    {
        double wrapped = fmod(time, info->duration);
        return wrapped < 0 ? wrapped + info->duration : wrapped;
    }
    return time < 0 ? 0 : time > info->duration ? info->duration : time;
}

static bool exclusive(L_AssetAnimation* group)
{
    L_AssetMotion* motion = group->asset->motion;
    for (size_t i = 0; i < motion->groupCount; ++i)
    {
        L_AssetAnimation* other =
            (L_AssetAnimation*)l_peek(motion->groups[i]._runtime, motion->groups[i]._id);
        if (other != group && !other->info.isStopped)
        {
            return false;
        }
    }
    return true;
}

bl_Status bl_getAssetContainerAnimationGroups(bl_AssetContainer handle, bl_AnimationGroups* out)
{
    L_GET(handle, L_ASSET, L_Asset, asset);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = asset->motion ? bl_AnimationGroups{asset->motion->groups, asset->motion->groupCount}
                         : bl_AnimationGroups{};
    return BL_OK;
}

bl_Status bl_getAnimationGroupInfo(bl_AnimationGroup handle, bl_AnimationGroupInfo* out)
{
    L_GET(handle, L_ANIMATION, L_AssetAnimation, group);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = group->info;
    return BL_OK;
}

bl_Status bl_playAnimation(bl_AnimationGroup handle)
{
    L_GET(handle, L_ANIMATION, L_AssetAnimation, group);
    if (!exclusive(group))
    {
        return BL_BUSY;
    }
    group->info.isPlaying = true;
    group->info.isStopped = false;
    return BL_OK;
}

bl_Status bl_pauseAnimation(bl_AnimationGroup handle)
{
    L_GET(handle, L_ANIMATION, L_AssetAnimation, group);
    group->info.isPlaying = false;
    return BL_OK;
}

bl_Status bl_stopAnimation(bl_AnimationGroup handle)
{
    L_GET(handle, L_ANIMATION, L_AssetAnimation, group);
    group->info.isPlaying = false;
    group->info.currentTime = 0;
    group->info.isStopped = true;
    return BL_OK;
}

bl_Status bl_setAnimationPlayback(bl_AnimationGroup handle, double speed, bool loop)
{
    L_GET(handle, L_ANIMATION, L_AssetAnimation, group);
    if (!isfinite(speed))
    {
        return BL_INVALID_ARGUMENT;
    }
    group->info.speedRatio = speed;
    group->info.loopAnimation = loop;
    return BL_OK;
}

bl_Status bl_goToFrame(bl_AnimationGroup handle, double frame)
{
    L_GET(handle, L_ANIMATION, L_AssetAnimation, group);
    if (!isfinite(frame))
    {
        return BL_INVALID_ARGUMENT;
    }
    if (!exclusive(group))
    {
        return BL_BUSY;
    }
    double time = sampleTime(&group->info, frame / 60);
    L_TRY(l_evaluateAssetMotion(group->asset, group, time, false));
    group->info.currentTime = time;
    group->info.isPlaying = false;
    return BL_OK;
}

bl_Status bl_selectAssetContainerAnimation(bl_AssetContainer handle, size_t index)
{
    L_GET(handle, L_ASSET, L_Asset, asset);
    L_AssetMotion* motion = asset->motion;
    if (!motion || index >= motion->groupCount)
    {
        return BL_INVALID_ARGUMENT;
    }
    L_AssetAnimation* selected =
        (L_AssetAnimation*)l_peek(handle._runtime, motion->groups[index]._id);
    L_TRY(l_evaluateAssetMotion(asset, selected, 0, true));
    for (size_t i = 0; i < motion->groupCount; ++i)
    {
        L_AssetAnimation* group = (L_AssetAnimation*)l_peek(handle._runtime, motion->groups[i]._id);
        group->info.currentTime = 0;
        group->info.isPlaying = group == selected;
        group->info.isStopped = group != selected;
    }
    return BL_OK;
}

bl_Status l_tickAssetMotion(L_Asset* asset, L_Scene* scene, double delta)
{
    L_AssetMotion* motion = asset->motion;
    bool ownsRoot = false;
    for (size_t i = 0; i < scene->memberCount; ++i)
    {
        ownsRoot |= scene->members[i]->record.id == asset->root._id;
    }
    if (!ownsRoot)
    {
        return BL_OK;
    }
    if (motion->lastFrame == scene->engine->frameSerial)
    {
        return BL_OK;
    }
    motion->lastFrame = scene->engine->frameSerial;
    L_AssetAnimation* active = NULL;
    double time = 0;
    for (size_t i = 0; i < motion->groupCount; ++i)
    {
        L_AssetAnimation* group =
            (L_AssetAnimation*)l_peek(scene->engine->runtime, motion->groups[i]._id);
        if (!group->info.isStopped)
        {
            active = group;
            double next = group->info.currentTime +
                          (group->info.isPlaying ? delta / 1000 * group->info.speedRatio : 0);
            if (!isfinite(next))
            {
                return BL_INVALID_ARGUMENT;
            }
            time = sampleTime(&group->info, next);
            break;
        }
    }
    L_TRY(l_evaluateAssetMotion(asset, active, time, false));
    if (active)
    {
        active->info.currentTime = time;
    }
    return BL_OK;
}
