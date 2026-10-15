#include "GltfInternal.h"
#include "AssetMotionInternal.h"

const cgltf_accessor* l_gltfInfluence(const cgltf_primitive* primitive, size_t set, bool joints)
{
    cgltf_attribute_type type = joints ? cgltf_attribute_type_joints : cgltf_attribute_type_weights;
    for (size_t i = 0; i < primitive->attributes_count; ++i)
    {
        const cgltf_attribute* attribute = primitive->attributes + i;
        if (attribute->type == type && attribute->index >= 0 && (size_t)attribute->index == set)
        {
            return attribute->data;
        }
    }
    return NULL;
}

bl_Status l_gltfInfluenceSets(const cgltf_primitive* primitive, size_t* sets, size_t* vertices)
{
    size_t joints = 0;
    size_t weights = 0;
    size_t count = 0;
    size_t largest = 0;
    for (size_t i = 0; i < primitive->attributes_count; ++i)
    {
        const cgltf_attribute* attribute = primitive->attributes + i;
        bool joint = attribute->type == cgltf_attribute_type_joints;
        if (!joint && attribute->type != cgltf_attribute_type_weights)
        {
            continue;
        }
        bool nameJoint = false;
        int index = l_assetInfluence({attribute->name, strlen(attribute->name)}, &nameJoint);
        const cgltf_accessor* accessor = attribute->data;
        bool integer = accessor && (accessor->component_type == cgltf_component_type_r_8u ||
                                    accessor->component_type == cgltf_component_type_r_16u);
        if (index < 0 || index != attribute->index || nameJoint != joint || !accessor ||
            accessor->type != cgltf_type_vec4 ||
            (joint ? (!integer || accessor->normalized)
                   : (integer ? !accessor->normalized
                              : accessor->component_type != cgltf_component_type_r_32f ||
                                    accessor->normalized)) ||
            (count && count != accessor->count))
        {
            return BL_INVALID_ARGUMENT;
        }
        count = accessor->count;
        largest = (size_t)index > largest ? (size_t)index : largest;
        joints += joint;
        weights += !joint;
        for (size_t j = 0; j < i; ++j)
        {
            if (primitive->attributes[j].type == attribute->type &&
                primitive->attributes[j].index == attribute->index)
            {
                return BL_INVALID_ARGUMENT;
            }
        }
    }
    if (joints != weights || (joints && largest != joints - 1))
    {
        return BL_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < joints; ++i)
    {
        if (!l_gltfInfluence(primitive, i, true) || !l_gltfInfluence(primitive, i, false))
        {
            return BL_INVALID_ARGUMENT;
        }
    }
    *sets = joints;
    *vertices = count;
    return BL_OK;
}

static bool ancestor(const cgltf_data* data, const cgltf_node* root, const cgltf_node* node)
{
    for (size_t i = 0; node && i < data->nodes_count; ++i)
    {
        if (node == root)
        {
            return true;
        }
        node = node->parent;
    }
    return false;
}

static bool inScene(const cgltf_data* data, const cgltf_scene* scene, const cgltf_node* node)
{
    for (size_t i = 0; i < scene->nodes_count; ++i)
    {
        if (ancestor(data, scene->nodes[i], node))
        {
            return true;
        }
    }
    return false;
}

static bl_Status skinPrimitive(const cgltf_primitive* primitive, const cgltf_skin* skin)
{
    size_t sets;
    size_t vertices;
    L_TRY(l_gltfInfluenceSets(primitive, &sets, &vertices));
    if (!sets)
    {
        return BL_INVALID_ARGUMENT;
    }
    for (size_t v = 0; v < vertices; ++v)
    {
        double total = 0;
        double integerSum = 0;
        bool integerOnly = true;
        for (size_t i = 0; i < sets; ++i)
        {
            const cgltf_accessor* weights = l_gltfInfluence(primitive, i, false);
            float joints[4];
            float values[4];
            L_TRY(l_gltfRead(l_gltfInfluence(primitive, i, true), v, joints, 4));
            L_TRY(l_gltfRead(weights, v, values, 4));
            if (weights->component_type == cgltf_component_type_r_32f)
            {
                integerOnly = false;
            }
            else
            {
                cgltf_accessor raw = *weights;
                raw.normalized = false;
                float rawValues[4];
                L_TRY(l_gltfRead(&raw, v, rawValues, 4));
                double factor = weights->component_type == cgltf_component_type_r_8u ? 257 : 1;
                for (size_t lane = 0; lane < 4; ++lane)
                {
                    integerSum += rawValues[lane] * factor;
                }
            }
            for (size_t lane = 0; lane < 4; ++lane)
            {
                if (joints[lane] >= skin->joints_count || values[lane] < 0)
                {
                    return BL_INVALID_ARGUMENT;
                }
                total += values[lane];
                if (values[lane] == 0)
                {
                    continue;
                }
                for (size_t previous = 0; previous <= i; ++previous)
                {
                    float oldJoints[4];
                    float oldWeights[4];
                    L_TRY(l_gltfRead(l_gltfInfluence(primitive, previous, true), v, oldJoints, 4));
                    L_TRY(
                        l_gltfRead(l_gltfInfluence(primitive, previous, false), v, oldWeights, 4));
                    size_t limit = previous == i ? lane : 4;
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
        if (!isfinite(total) || total <= 0 || (integerOnly && integerSum != 65535))
        {
            return BL_INVALID_ARGUMENT;
        }
    }
    return BL_OK;
}

static bl_Status validateSkin(L_Gltf* gltf, const cgltf_skin* skin)
{
    const cgltf_data* data = gltf->data;
    if (!skin->joints_count || skin->joints_count > UINT32_MAX)
    {
        return BL_INVALID_ARGUMENT;
    }
    if (skin->extensions_count)
    {
        return BL_UNSUPPORTED;
    }
    for (size_t i = 0; i < skin->joints_count; ++i)
    {
        if (!skin->joints[i])
        {
            return BL_INVALID_ARGUMENT;
        }
        for (size_t j = 0; j < i; ++j)
        {
            if (skin->joints[i] == skin->joints[j])
            {
                return BL_INVALID_ARGUMENT;
            }
        }
    }
    const cgltf_node* root = skin->skeleton ? skin->skeleton : skin->joints[0];
    bool common = false;
    for (size_t attempt = 0; root && attempt < data->nodes_count; ++attempt)
    {
        common = true;
        for (size_t i = 0; i < skin->joints_count; ++i)
        {
            common &= ancestor(data, root, skin->joints[i]);
        }
        if (common || skin->skeleton)
        {
            break;
        }
        root = root->parent;
    }
    if (!common)
    {
        return BL_INVALID_ARGUMENT;
    }
    const cgltf_accessor* ibm = skin->inverse_bind_matrices;
    if (ibm)
    {
        if (ibm->type != cgltf_type_mat4 || ibm->component_type != cgltf_component_type_r_32f ||
            ibm->normalized || ibm->count < skin->joints_count)
        {
            return BL_INVALID_ARGUMENT;
        }
        for (size_t i = 0; i < ibm->count; ++i)
        {
            float matrix[16];
            L_TRY(l_gltfRead(ibm, i, matrix, 16));
            if (matrix[3] || matrix[7] || matrix[11] || matrix[15] != 1)
            {
                return BL_INVALID_ARGUMENT;
            }
        }
    }
    for (size_t i = 0; i < data->nodes_count; ++i)
    {
        const cgltf_node* node = data->nodes + i;
        if (node->skin != skin)
        {
            continue;
        }
        if (!node->mesh)
        {
            return BL_INVALID_ARGUMENT;
        }
        if (node->has_mesh_gpu_instancing)
        {
            gltf->reason = "Skinned EXT_mesh_gpu_instancing is not implemented";
            return BL_UNSUPPORTED;
        }
        for (size_t scene = 0; scene < data->scenes_count; ++scene)
        {
            if (inScene(data, data->scenes + scene, node) &&
                !inScene(data, data->scenes + scene, root))
            {
                return BL_INVALID_ARGUMENT;
            }
        }
        for (size_t p = 0; p < node->mesh->primitives_count; ++p)
        {
            L_TRY(skinPrimitive(node->mesh->primitives + p, skin));
        }
    }
    return BL_OK;
}

static bl_Status validateClip(const cgltf_animation* clip)
{
    if (!clip->samplers_count || !clip->channels_count || clip->samplers_count > UINT32_MAX ||
        clip->channels_count > UINT32_MAX)
    {
        return BL_INVALID_ARGUMENT;
    }
    if (clip->extensions_count)
    {
        return BL_UNSUPPORTED;
    }
    for (size_t i = 0; i < clip->samplers_count; ++i)
    {
        const cgltf_animation_sampler* sampler = clip->samplers + i;
        const cgltf_accessor* input = sampler->input;
        const cgltf_accessor* output = sampler->output;
        if (!input || !output || input->type != cgltf_type_scalar ||
            input->component_type != cgltf_component_type_r_32f || input->normalized ||
            !input->has_min || !input->has_max ||
            (sampler->interpolation == cgltf_interpolation_type_cubic_spline && input->count < 2))
        {
            return BL_INVALID_ARGUMENT;
        }
        if (sampler->extensions_count)
        {
            return BL_UNSUPPORTED;
        }
        double previous = -1;
        for (size_t j = 0; j < input->count; ++j)
        {
            float time;
            L_TRY(l_gltfRead(input, j, &time, 1));
            if (time < 0 || time <= previous)
            {
                return BL_INVALID_ARGUMENT;
            }
            previous = time;
        }
        for (size_t j = 0; j < output->count; ++j)
        {
            float values[16];
            L_TRY(l_gltfRead(output, j, values, cgltf_num_components(output->type)));
        }
    }
    for (size_t i = 0; i < clip->channels_count; ++i)
    {
        const cgltf_animation_channel* channel = clip->channels + i;
        if (!channel->sampler || channel->target_path == cgltf_animation_path_type_invalid)
        {
            return BL_INVALID_ARGUMENT;
        }
        if (channel->extensions_count || channel->target_path == cgltf_animation_path_type_weights)
        {
            return BL_UNSUPPORTED;
        }
        if (!channel->target_node)
        {
            continue;
        }
        if (channel->target_node->has_matrix)
        {
            return BL_INVALID_ARGUMENT;
        }
        for (size_t j = 0; j < i; ++j)
        {
            if (clip->channels[j].target_node == channel->target_node &&
                clip->channels[j].target_path == channel->target_path)
            {
                return BL_INVALID_ARGUMENT;
            }
        }
        const cgltf_animation_sampler* sampler = channel->sampler;
        const cgltf_accessor* output = sampler->output;
        size_t multiplier = sampler->interpolation == cgltf_interpolation_type_cubic_spline ? 3 : 1;
        bool rotation = channel->target_path == cgltf_animation_path_type_rotation;
        bool integer = output->component_type == cgltf_component_type_r_8 ||
                       output->component_type == cgltf_component_type_r_8u ||
                       output->component_type == cgltf_component_type_r_16 ||
                       output->component_type == cgltf_component_type_r_16u;
        if (output->type != (rotation ? cgltf_type_vec4 : cgltf_type_vec3) ||
            output->count != sampler->input->count * multiplier ||
            (output->component_type == cgltf_component_type_r_32f
                 ? output->normalized
                 : !rotation || !integer || !output->normalized))
        {
            return BL_INVALID_ARGUMENT;
        }
        if (rotation)
        {
            for (size_t j = 0; j < sampler->input->count; ++j)
            {
                float value[4];
                L_TRY(l_gltfRead(output, j * multiplier + multiplier / 3, value, 4));
                bl_Quat q = {value[0], value[1], value[2], value[3]};
                L_TRY(l_normalizeAssetQuaternion(&q));
            }
        }
    }
    return BL_OK;
}

bl_Status l_gltfValidateMotion(L_Gltf* gltf)
{
    const cgltf_data* data = gltf->data;
    if (data->nodes_count > INT32_MAX || data->skins_count > UINT32_MAX ||
        data->animations_count > UINT32_MAX)
    {
        return BL_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < data->meshes_count; ++i)
    {
        for (size_t j = 0; j < data->meshes[i].primitives_count; ++j)
        {
            size_t sets;
            size_t vertices;
            L_TRY(l_gltfInfluenceSets(data->meshes[i].primitives + j, &sets, &vertices));
        }
    }
    for (size_t i = 0; i < data->skins_count; ++i)
    {
        L_TRY(validateSkin(gltf, data->skins + i));
    }
    for (size_t i = 0; i < data->animations_count; ++i)
    {
        L_TRY(validateClip(data->animations + i));
    }
    return BL_OK;
}
