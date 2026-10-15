#include "AssetMotionInternal.h"

static bool motionSpan(const void* data, size_t count, size_t size, size_t alignment)
{
    size_t bytes;
    return count <= UINT32_MAX && l_size(count, size, &bytes) && l_allocationFits(bytes) &&
           l_typedSpan(data, count, alignment);
}

int l_assetInfluence(bl_String semantic, bool* joints)
{
    if (semantic.length < 7)
    {
        return -1;
    }
    bool joint = !memcmp(semantic.data, "JOINTS_", 7);
    bool weight = semantic.length >= 8 && !memcmp(semantic.data, "WEIGHTS_", 8);
    if (!joint && !weight)
    {
        return -1;
    }
    *joints = joint;
    size_t first = joint ? 7 : 8;
    if (semantic.length == first || (semantic.length > first + 1 && semantic.data[first] == '0'))
    {
        return -2;
    }
    unsigned value = 0;
    for (size_t i = first; i < semantic.length; ++i)
    {
        unsigned digit = (unsigned)(semantic.data[i] - '0');
        if (digit > 9 || value > (INT32_MAX - digit) / 10)
        {
            return -2;
        }
        value = value * 10 + digit;
    }
    return (int)value;
}

unsigned l_assetAnimationPath(bl_String path)
{
    return l_name(path, "translation") ? 0u
           : l_name(path, "rotation")  ? 1u
           : l_name(path, "scale")     ? 2u
           : l_name(path, "weights")   ? 3u
                                       : UINT32_MAX;
}

unsigned l_assetInterpolation(const bl_AssetAnimationSampler* sampler)
{
    return !sampler->hasInterpolation || l_name(sampler->interpolation, "LINEAR") ? 0u
           : l_name(sampler->interpolation, "STEP")                               ? 1u
           : l_name(sampler->interpolation, "CUBICSPLINE")                        ? 2u
                                                                                  : UINT32_MAX;
}

static int32_t parentIndex(const bl_ParsedGltf* data, uint32_t child)
{
    for (size_t i = 0; i < data->nodeCount; ++i)
    {
        for (size_t j = 0; j < data->nodes[i].childCount; ++j)
        {
            if (data->nodes[i].children[j] == child)
            {
                return (int32_t)i;
            }
        }
    }
    return -1;
}

static bool ancestor(const bl_ParsedGltf* data, uint32_t root, uint32_t node)
{
    for (size_t i = 0; i < data->nodeCount; ++i)
    {
        if (node == root)
        {
            return true;
        }
        int32_t parent = parentIndex(data, node);
        if (parent < 0)
        {
            return false;
        }
        node = (uint32_t)parent;
    }
    return false;
}

static bool selected(const bl_ParsedGltf* data, uint32_t node)
{
    uint32_t scene = data->defaultScene.present ? data->defaultScene.value : 0;
    if (scene >= data->sceneCount)
    {
        return false;
    }
    for (size_t i = 0; i < data->scenes[scene].rootCount; ++i)
    {
        if (ancestor(data, data->scenes[scene].roots[i], node))
        {
            return true;
        }
    }
    return false;
}

static const bl_AssetAttribute* influenceAttribute(const bl_AssetPrimitive* primitive, int set,
                                                   bool joint)
{
    for (size_t i = 0; i < primitive->attributeCount; ++i)
    {
        bool isJoint = false;
        if (l_assetInfluence(primitive->attributes[i].semantic, &isJoint) == set &&
            isJoint == joint)
        {
            return primitive->attributes + i;
        }
    }
    return NULL;
}

static bl_Status influenceValues(const bl_ParsedGltf* data, const bl_ParsedGltf4* full,
                                 const bl_AssetPrimitive* primitive, int set, size_t vertex,
                                 double joints[4], double weights[4], double* integerSum,
                                 bool* integers)
{
    const bl_AssetAttribute* joint = influenceAttribute(primitive, set, true);
    const bl_AssetAttribute* weight = influenceAttribute(primitive, set, false);
    if (!joint || !weight)
    {
        return BL_INVALID_ARGUMENT;
    }
    L_AssetAccessorReader reader;
    L_TRY(l_assetAccessorReader(data, joint->accessor, full, &reader));
    L_TRY(l_assetAccessorElement(&reader, vertex, joints, false));
    L_TRY(l_assetAccessorReader(data, weight->accessor, full, &reader));
    L_TRY(l_assetAccessorElement(&reader, vertex, weights));
    if (reader.accessor->componentType == BL_ASSET_COMPONENT_F32)
    {
        *integers = false;
    }
    else
    {
        double raw[4];
        L_TRY(l_assetAccessorElement(&reader, vertex, raw, false));
        double factor = reader.accessor->componentType == BL_ASSET_COMPONENT_U8 ? 257 : 1;
        for (size_t i = 0; i < 4; ++i)
        {
            *integerSum += raw[i] * factor;
        }
    }
    return BL_OK;
}

static bl_Status skinPrimitive(const bl_ParsedGltf6* full, const bl_AssetPrimitive* primitive,
                               const bl_AssetSkin* skin)
{
    const bl_ParsedGltf* data = &full->document5.document4.document3.document;
    size_t sets = 0;
    for (size_t i = 0; i < primitive->attributeCount; ++i)
    {
        bool joint = false;
        int set = l_assetInfluence(primitive->attributes[i].semantic, &joint);
        if (set >= 0 && joint)
        {
            ++sets;
        }
    }
    if (!sets)
    {
        return BL_INVALID_ARGUMENT;
    }
    const bl_AssetAttribute* first = influenceAttribute(primitive, 0, true);
    if (!first)
    {
        return BL_INVALID_ARGUMENT;
    }
    size_t vertices = data->accessors[first->accessor].count;
    for (size_t v = 0; v < vertices; ++v)
    {
        double total = 0;
        double integerSum = 0;
        bool integers = true;
        for (size_t s = 0; s < sets; ++s)
        {
            double joints[4];
            double weights[4];
            L_TRY(influenceValues(data, &full->document5.document4, primitive, (int)s, v, joints,
                                  weights, &integerSum, &integers));
            for (size_t lane = 0; lane < 4; ++lane)
            {
                if (joints[lane] >= skin->jointCount || !isfinite(weights[lane]) ||
                    weights[lane] < 0)
                {
                    return BL_INVALID_ARGUMENT;
                }
                total += weights[lane];
                if (weights[lane] == 0)
                {
                    continue;
                }
                for (size_t previous = 0; previous <= s; ++previous)
                {
                    double oldJoints[4];
                    double oldWeights[4];
                    double ignored = 0;
                    bool unused = true;
                    L_TRY(influenceValues(data, &full->document5.document4, primitive,
                                          (int)previous, v, oldJoints, oldWeights, &ignored,
                                          &unused));
                    size_t limit = previous == s ? lane : 4;
                    for (size_t k = 0; k < limit; ++k)
                    {
                        if (oldWeights[k] > 0 && oldJoints[k] == joints[lane])
                        {
                            return BL_INVALID_ARGUMENT;
                        }
                    }
                }
            }
        }
        if (!isfinite(total) || total <= 0 || (integers && integerSum != 65535))
        {
            return BL_INVALID_ARGUMENT;
        }
    }
    return BL_OK;
}

bl_Status l_validateAssetMotion(const bl_ParsedGltf6* full)
{
    const bl_ParsedGltf* data = &full->document5.document4.document3.document;
    const bl_ParsedGltf4* accessors = &full->document5.document4;
    if (data->nodeCount > INT32_MAX ||
        !motionSpan(full->skins, data->skinCount, sizeof(bl_AssetSkin), alignof(bl_AssetSkin)) ||
        !motionSpan(full->animations, data->animationCount, sizeof(bl_AssetAnimation),
                    alignof(bl_AssetAnimation)))
    {
        return BL_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < data->skinCount; ++i)
    {
        const bl_AssetSkin* skin = full->skins + i;
        if (!skin->jointCount ||
            !motionSpan(skin->joints, skin->jointCount, sizeof(uint32_t), alignof(uint32_t)) ||
            (skin->hasName && !l_string(skin->name)) ||
            (skin->skeleton.present && skin->skeleton.value >= data->nodeCount))
        {
            return BL_INVALID_ARGUMENT;
        }
        if (skin->unsupportedFeatures)
        {
            return BL_UNSUPPORTED;
        }
        for (size_t j = 0; j < skin->jointCount; ++j)
        {
            if (skin->joints[j] >= data->nodeCount)
            {
                return BL_INVALID_ARGUMENT;
            }
            for (size_t k = 0; k < j; ++k)
            {
                if (skin->joints[j] == skin->joints[k])
                {
                    return BL_INVALID_ARGUMENT;
                }
            }
        }
        uint32_t root = skin->skeleton.present ? skin->skeleton.value : skin->joints[0];
        bool common = false;
        for (size_t attempt = 0; attempt < data->nodeCount; ++attempt)
        {
            common = true;
            for (size_t j = 0; j < skin->jointCount; ++j)
            {
                common &= ancestor(data, root, skin->joints[j]);
            }
            if (common || skin->skeleton.present)
            {
                break;
            }
            int32_t parent = parentIndex(data, root);
            if (parent < 0)
            {
                break;
            }
            root = (uint32_t)parent;
        }
        if (!common)
        {
            return BL_INVALID_ARGUMENT;
        }
        if (skin->inverseBindMatrices.present)
        {
            L_AssetAccessorReader reader;
            L_TRY(l_assetAccessorReader(data, skin->inverseBindMatrices.value, accessors, &reader));
            if (reader.accessor->shape != BL_ASSET_MAT4 ||
                reader.accessor->componentType != BL_ASSET_COMPONENT_F32 || reader.normalized ||
                reader.accessor->count < skin->jointCount)
            {
                return BL_INVALID_ARGUMENT;
            }
            for (size_t j = 0; j < reader.accessor->count; ++j)
            {
                double matrix[16];
                L_TRY(l_assetAccessorElement(&reader, j, matrix));
                for (size_t k = 0; k < 16; ++k)
                {
                    if (!isfinite(matrix[k]))
                    {
                        return BL_INVALID_ARGUMENT;
                    }
                }
                if (matrix[3] || matrix[7] || matrix[11] || matrix[15] != 1)
                {
                    return BL_INVALID_ARGUMENT;
                }
            }
        }
        for (size_t n = 0; n < data->nodeCount; ++n)
        {
            const bl_AssetNode* node = data->nodes + n;
            if (node->skin.present && node->skin.value == i)
            {
                if (selected(data, (uint32_t)n) && !selected(data, root))
                {
                    return BL_INVALID_ARGUMENT;
                }
                const bl_AssetMesh* mesh = data->meshes + node->mesh.value;
                for (size_t p = 0; p < mesh->primitiveCount; ++p)
                {
                    L_TRY(skinPrimitive(full, mesh->primitives + p, skin));
                }
            }
        }
    }
    for (size_t i = 0; i < data->animationCount; ++i)
    {
        const bl_AssetAnimation* clip = full->animations + i;
        if ((clip->hasName && !l_string(clip->name)) || !clip->samplerCount ||
            !clip->channelCount ||
            !motionSpan(clip->samplers, clip->samplerCount, sizeof(bl_AssetAnimationSampler),
                        alignof(bl_AssetAnimationSampler)) ||
            !motionSpan(clip->channels, clip->channelCount, sizeof(bl_AssetAnimationChannel),
                        alignof(bl_AssetAnimationChannel)))
        {
            return BL_INVALID_ARGUMENT;
        }
        if (clip->unsupportedFeatures)
        {
            return BL_UNSUPPORTED;
        }
        for (size_t j = 0; j < clip->samplerCount; ++j)
        {
            const bl_AssetAnimationSampler* sampler = clip->samplers + j;
            if ((sampler->hasInterpolation && !l_string(sampler->interpolation)))
            {
                return BL_INVALID_ARGUMENT;
            }
            if (sampler->unsupportedFeatures || l_assetInterpolation(sampler) == UINT32_MAX)
            {
                return BL_UNSUPPORTED;
            }
            L_AssetAccessorReader input;
            L_AssetAccessorReader output;
            L_TRY(l_assetAccessorReader(data, sampler->input, accessors, &input));
            L_TRY(l_assetAccessorReader(data, sampler->output, accessors, &output));
            if (input.accessor->shape != BL_ASSET_SCALAR ||
                input.accessor->componentType != BL_ASSET_COMPONENT_F32 || input.normalized ||
                !accessors->accessorInputs[sampler->input].hasMin ||
                !accessors->accessorInputs[sampler->input].hasMax ||
                (l_assetInterpolation(sampler) == 2 && input.accessor->count < 2))
            {
                return BL_INVALID_ARGUMENT;
            }
            double previous = -1;
            for (size_t k = 0; k < input.accessor->count; ++k)
            {
                double time;
                L_TRY(l_assetAccessorElement(&input, k, &time));
                if (!isfinite(time) || time < 0 || time <= previous)
                {
                    return BL_INVALID_ARGUMENT;
                }
                previous = time;
            }
            for (size_t k = 0; k < output.accessor->count; ++k)
            {
                double values[16];
                L_TRY(l_assetAccessorElement(&output, k, values));
                for (size_t c = 0; c < output.rows * output.columns; ++c)
                {
                    if (!isfinite(values[c]) || !isfinite((float)values[c]))
                    {
                        return BL_INVALID_ARGUMENT;
                    }
                }
            }
        }
        for (size_t j = 0; j < clip->channelCount; ++j)
        {
            const bl_AssetAnimationChannel* channel = clip->channels + j;
            if (channel->sampler >= clip->samplerCount || !l_string(channel->path) ||
                (channel->node.present && channel->node.value >= data->nodeCount))
            {
                return BL_INVALID_ARGUMENT;
            }
            if (channel->unsupportedFeatures)
            {
                return BL_UNSUPPORTED;
            }
            unsigned path = l_assetAnimationPath(channel->path);
            if (path == UINT32_MAX)
            {
                return BL_INVALID_ARGUMENT;
            }
            if (!channel->node.present)
            {
                continue;
            }
            if (path == 3)
            {
                return BL_UNSUPPORTED;
            }
            if (data->nodes[channel->node.value].hasMatrix)
            {
                return BL_INVALID_ARGUMENT;
            }
            for (size_t k = 0; k < j; ++k)
            {
                if (clip->channels[k].node.present &&
                    clip->channels[k].node.value == channel->node.value &&
                    l_equal(clip->channels[k].path, channel->path))
                {
                    return BL_INVALID_ARGUMENT;
                }
            }
            const bl_AssetAnimationSampler* sampler = clip->samplers + channel->sampler;
            const bl_AssetAccessor* input = data->accessors + sampler->input;
            const bl_AssetAccessor* output = data->accessors + sampler->output;
            bool rotation = path == 1;
            bool integer = output->componentType == BL_ASSET_COMPONENT_I8 ||
                           output->componentType == BL_ASSET_COMPONENT_U8 ||
                           output->componentType == BL_ASSET_COMPONENT_I16 ||
                           output->componentType == BL_ASSET_COMPONENT_U16;
            size_t multiplier = l_assetInterpolation(sampler) == 2 ? 3 : 1;
            if (output->shape != (rotation ? BL_ASSET_VEC4 : BL_ASSET_VEC3) ||
                output->count != input->count * multiplier ||
                (output->componentType == BL_ASSET_COMPONENT_F32
                     ? output->normalized
                     : !rotation || !integer || !output->normalized))
            {
                return BL_INVALID_ARGUMENT;
            }
            if (rotation)
            {
                L_AssetAccessorReader reader;
                L_TRY(l_assetAccessorReader(data, sampler->output, accessors, &reader));
                for (size_t k = 0; k < input->count; ++k)
                {
                    double values[4];
                    L_TRY(l_assetAccessorElement(&reader, k * multiplier + multiplier / 3, values));
                    bl_Quat quaternion = {values[0], values[1], values[2], values[3]};
                    L_TRY(l_normalizeAssetQuaternion(&quaternion));
                }
            }
        }
    }
    return BL_OK;
}
