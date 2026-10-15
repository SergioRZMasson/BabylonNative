#include "GltfInternal.h"
#include "AssetMotionInternal.h"

static void* owned(L_Gltf* gltf, size_t count, size_t stride)
{
    size_t bytes;
    if (!l_size(count, stride, &bytes) || !l_allocationFits(bytes))
    {
        return NULL;
    }
    return l_alloc(gltf->runtime, bytes);
}

static bl_Status sampler(L_Gltf* gltf, const cgltf_animation_sampler* source,
                         L_AssetSampler* target)
{
    target->count = source->input->count;
    target->components = cgltf_num_components(source->output->type);
    target->interpolation = (unsigned)source->interpolation;
    size_t values;
    if (!l_size(source->output->count, target->components, &values))
    {
        return BL_INVALID_ARGUMENT;
    }
    target->times = (float*)owned(gltf, target->count, sizeof(float));
    target->values = (float*)owned(gltf, values, sizeof(float));
    if (!target->times || !target->values)
    {
        return BL_OUT_OF_MEMORY;
    }
    for (size_t i = 0; i < target->count; ++i)
    {
        L_TRY(l_gltfRead(source->input, i, target->times + i, 1));
    }
    for (size_t i = 0; i < source->output->count; ++i)
    {
        L_TRY(l_gltfRead(source->output, i, target->values + i * target->components,
                         target->components));
    }
    return BL_OK;
}

bl_Status l_gltfCreateMotion(L_Gltf* gltf, L_Asset* asset)
{
    const cgltf_data* data = gltf->data;
    if (!data->skins_count && !data->animations_count)
    {
        return BL_OK;
    }
    L_TRY(l_createAssetMotion(asset, data->nodes_count, data->skins_count, data->animations_count));
    L_AssetMotion* motion = asset->motion;
    size_t count = 0;
    for (size_t i = 0; i < data->nodes_count; ++i)
    {
        const cgltf_node* node = data->nodes + i;
        motion->parents[i] = node->parent ? (int32_t)(node->parent - data->nodes) : -1;
        motion->rest[i] = {
            {node->translation[0], node->translation[1], node->translation[2]},
            {node->rotation[0], node->rotation[1], node->rotation[2], node->rotation[3]},
            {node->scale[0], node->scale[1], node->scale[2]}};
        L_TRY(l_normalizeAssetQuaternion(&motion->rest[i].rotationQuaternion));
        bl_Quat* rotation = &motion->rest[i].rotationQuaternion;
        rotation->x = (float)rotation->x;
        rotation->y = (float)rotation->y;
        rotation->z = (float)rotation->z;
        rotation->w = (float)rotation->w;
        if (node->has_matrix)
        {
            motion->matrixLocked[i] = true;
            motion->rest[i] = {{}, {0, 0, 0, 1}, {1, 1, 1}};
            for (size_t j = 0; j < 16; ++j)
            {
                motion->locals[i].values[j] = node->matrix[j];
            }
        }
        if (!node->parent)
        {
            motion->order[count++] = (uint32_t)i;
        }
    }
    for (size_t i = 0; i < count; ++i)
    {
        const cgltf_node* node = data->nodes + motion->order[i];
        for (size_t j = 0; j < node->children_count; ++j)
        {
            if (count == data->nodes_count)
            {
                return BL_INVALID_ARGUMENT;
            }
            motion->order[count++] = (uint32_t)(node->children[j] - data->nodes);
        }
    }
    if (count != data->nodes_count)
    {
        return BL_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < data->skins_count; ++i)
    {
        const cgltf_skin* source = data->skins + i;
        L_AssetSkinData* skin = motion->skins + i;
        skin->count = source->joints_count;
        skin->joints = (uint32_t*)owned(gltf, skin->count, sizeof(uint32_t));
        skin->inverseBind = (float*)owned(gltf, skin->count, 16 * sizeof(float));
        skin->palette = (float*)owned(gltf, skin->count, 16 * sizeof(float));
        if (!skin->joints || !skin->inverseBind || !skin->palette)
        {
            return BL_OUT_OF_MEMORY;
        }
        for (size_t j = 0; j < skin->count; ++j)
        {
            skin->joints[j] = (uint32_t)(source->joints[j] - data->nodes);
            float* matrix = skin->inverseBind + j * 16;
            if (source->inverse_bind_matrices)
            {
                L_TRY(l_gltfRead(source->inverse_bind_matrices, j, matrix, 16));
            }
            else
            {
                matrix[0] = 1;
                matrix[5] = 1;
                matrix[10] = 1;
                matrix[15] = 1;
            }
        }
    }
    for (size_t i = 0; i < data->animations_count; ++i)
    {
        const cgltf_animation* source = data->animations + i;
        char fallback[64];
        snprintf(fallback, sizeof(fallback), "animation_%zu", i);
        const char* name = source->name && source->name[0] ? source->name : fallback;
        L_AssetAnimation* clip;
        L_TRY(l_createAssetAnimation(asset, i, {name, strlen(name)}, source->samplers_count,
                                     source->channels_count, &clip));
        for (size_t j = 0; j < source->samplers_count; ++j)
        {
            L_TRY(sampler(gltf, source->samplers + j, clip->samplers + j));
            const L_AssetSampler* input = clip->samplers + j;
            double duration = input->times[input->count - 1];
            clip->info.duration = duration > clip->info.duration ? duration : clip->info.duration;
        }
        for (size_t j = 0; j < source->channels_count; ++j)
        {
            const cgltf_animation_channel* channel = source->channels + j;
            uint32_t node =
                channel->target_node ? (uint32_t)(channel->target_node - data->nodes) : UINT32_MAX;
            unsigned path =
                (unsigned)channel->target_path - (unsigned)cgltf_animation_path_type_translation;
            clip->channels[j] = {(uint32_t)(channel->sampler - source->samplers), node, path};
            if (node != UINT32_MAX)
            {
                motion->targets[node] |= (uint8_t)(1u << path);
            }
        }
    }
    return BL_OK;
}

bl_Status l_gltfBindSkin(L_Gltf* gltf, L_Asset* asset, const cgltf_node* node,
                         const cgltf_primitive* primitive, L_Mesh* mesh)
{
    if (!node->skin)
    {
        return BL_OK;
    }
    size_t sets;
    size_t vertices;
    L_TRY(l_gltfInfluenceSets(primitive, &sets, &vertices));
    size_t influences;
    if (!l_size(sets, 4, &influences) || vertices != mesh->geometry.vertices)
    {
        return BL_INVALID_ARGUMENT;
    }
    L_TRY(l_createAssetDeformation(asset, (size_t)(node->skin - gltf->data->skins), influences,
                                   mesh));
    L_AssetDeformation* deformation = mesh->skin;
    for (size_t v = 0; v < vertices; ++v)
    {
        double total = 0;
        for (size_t i = 0; i < sets; ++i)
        {
            float joints[4];
            float weights[4];
            L_TRY(l_gltfRead(l_gltfInfluence(primitive, i, true), v, joints, 4));
            L_TRY(l_gltfRead(l_gltfInfluence(primitive, i, false), v, weights, 4));
            for (size_t lane = 0; lane < 4; ++lane)
            {
                size_t offset = v * influences + i * 4 + lane;
                deformation->joints[offset] = (uint32_t)joints[lane];
                deformation->weights[offset] = weights[lane];
                total += weights[lane];
            }
        }
        for (size_t i = 0; i < influences; ++i)
        {
            size_t offset = v * influences + i;
            deformation->weights[offset] = (float)(deformation->weights[offset] / total);
        }
    }
    return BL_OK;
}
