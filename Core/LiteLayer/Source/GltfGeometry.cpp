#include "GltfInternal.h"

static bl_Status stream(bl_Runtime* runtime, const cgltf_accessor* accessor, size_t components,
                        bool color, bl_F32Span* out)
{
    size_t sourceComponents = cgltf_num_components(accessor->type);
    bool integer = accessor->component_type == cgltf_component_type_r_8u ||
                   accessor->component_type == cgltf_component_type_r_16u;
    cgltf_type expected = components == 2   ? cgltf_type_vec2
                          : components == 3 ? cgltf_type_vec3
                                            : cgltf_type_vec4;
    if ((accessor->type != expected && !(color && accessor->type == cgltf_type_vec3)) ||
        (accessor->component_type == cgltf_component_type_r_32f
             ? accessor->normalized
             : !integer || !accessor->normalized || (!color && components != 2)))
    {
        return BL_UNSUPPORTED;
    }
    if (accessor->buffer_view && (accessor->offset % 4 || accessor->stride % 4))
    {
        return BL_INVALID_ARGUMENT;
    }
    size_t count;
    size_t bytes;
    if (!l_size(accessor->count, components, &count) || !l_size(count, sizeof(float), &bytes) ||
        bytes > UINT32_MAX)
    {
        return BL_INVALID_ARGUMENT;
    }
    float* values = (float*)l_alloc(runtime, bytes);
    if (!values)
    {
        return BL_OUT_OF_MEMORY;
    }
    *out = {values, count};
    for (size_t i = 0; i < accessor->count; ++i)
    {
        float element[16] = {};
        L_TRY(l_gltfRead(accessor, i, element, sourceComponents));
        for (size_t j = 0; j < components; ++j)
        {
            float value = j < sourceComponents ? element[j] : 1;
            if (color && (value < 0 || value > 1))
            {
                return BL_INVALID_ARGUMENT;
            }
            values[i * components + j] = value;
        }
    }
    return BL_OK;
}

static bl_Status indices(bl_Runtime* runtime, const cgltf_primitive* source, size_t vertices,
                         bl_U32Span* out)
{
    bool triangles = source->type == cgltf_primitive_type_triangles;
    if (!triangles && source->type != cgltf_primitive_type_triangle_strip &&
        source->type != cgltf_primitive_type_triangle_fan)
    {
        return BL_UNSUPPORTED;
    }
    const cgltf_accessor* accessor = source->indices;
    size_t count = accessor ? accessor->count : vertices;
    if ((triangles && count % 3) || (!triangles && count < 3))
    {
        return BL_INVALID_ARGUMENT;
    }
    if (accessor && (accessor->type != cgltf_type_scalar || accessor->normalized ||
                     (accessor->component_type != cgltf_component_type_r_8u &&
                      accessor->component_type != cgltf_component_type_r_16u &&
                      accessor->component_type != cgltf_component_type_r_32u) ||
                     accessor->stride != cgltf_component_size(accessor->component_type)))
    {
        return BL_INVALID_ARGUMENT;
    }
    size_t reserved = accessor && accessor->component_type == cgltf_component_type_r_8u ? UINT8_MAX
                      : accessor && accessor->component_type == cgltf_component_type_r_16u
                          ? UINT16_MAX
                          : UINT32_MAX;
    size_t resultCount;
    size_t bytes;
    if (!l_size(triangles ? count : count - 2, triangles ? 1 : 3, &resultCount) ||
        !l_size(resultCount, sizeof(uint32_t), &bytes) || bytes > UINT32_MAX)
    {
        return BL_INVALID_ARGUMENT;
    }
    uint32_t* result = (uint32_t*)l_alloc(runtime, bytes ? bytes : sizeof(uint32_t));
    if (!result)
    {
        return BL_OUT_OF_MEMORY;
    }
    *out = {result, resultCount};
    for (size_t i = 0; i < resultCount; ++i)
    {
        size_t index = i;
        if (!triangles)
        {
            size_t triangle = i / 3;
            size_t corner = i % 3;
            if (source->type == cgltf_primitive_type_triangle_fan)
            {
                index = corner == 0 ? triangle + 1 : corner == 1 ? triangle + 2 : 0;
            }
            else
            {
                index = corner == 0   ? triangle
                        : corner == 1 ? triangle + 1 + triangle % 2
                                      : triangle + 2 - triangle % 2;
            }
        }
        size_t value = accessor ? cgltf_accessor_read_index(accessor, index) : index;
        if (value >= vertices || (accessor && value == reserved))
        {
            return BL_INVALID_ARGUMENT;
        }
        result[i] = (uint32_t)value;
    }
    return BL_OK;
}

static bl_Status instances(L_Gltf* gltf, const cgltf_node* source, bl_Mesh mesh)
{
    if (!source->has_mesh_gpu_instancing)
    {
        return BL_OK;
    }
    const cgltf_mesh_gpu_instancing* input = &source->mesh_gpu_instancing;
    const cgltf_accessor* accessors[3] = {};
    size_t count = 0;
    for (size_t i = 0; i < input->attributes_count; ++i)
    {
        const cgltf_attribute* attribute = input->attributes + i;
        int slot = !strcmp(attribute->name, "TRANSLATION") ? 0
                   : !strcmp(attribute->name, "ROTATION")  ? 1
                   : !strcmp(attribute->name, "SCALE")     ? 2
                                                           : -1;
        if (slot < 0)
        {
            return BL_UNSUPPORTED;
        }
        if (accessors[slot] || (count && count != attribute->data->count) ||
            attribute->data->type != (slot == 1 ? cgltf_type_vec4 : cgltf_type_vec3) ||
            attribute->data->normalized ||
            attribute->data->component_type != cgltf_component_type_r_32f)
        {
            return BL_INVALID_ARGUMENT;
        }
        accessors[slot] = attribute->data;
        count = attribute->data->count;
    }
    size_t bytes;
    if (!count || !l_size(count, sizeof(float) * 16, &bytes) || bytes > UINT32_MAX)
    {
        return BL_INVALID_ARGUMENT;
    }
    float* matrices = (float*)l_alloc(gltf->runtime, bytes);
    if (!matrices)
    {
        return BL_OUT_OF_MEMORY;
    }
    bl_Status status = BL_OK;
    for (size_t i = 0; i < count && status == BL_OK; ++i)
    {
        cgltf_node node = {};
        node.rotation[3] = 1;
        node.scale[0] = 1;
        node.scale[1] = 1;
        node.scale[2] = 1;
        float* values[3] = {node.translation, node.rotation, node.scale};
        for (size_t j = 0; j < 3 && status == BL_OK; ++j)
        {
            if (accessors[j])
            {
                status = l_gltfRead(accessors[j], i, values[j], j == 1 ? 4 : 3);
            }
        }
        if ((double)node.scale[0] * node.scale[1] * node.scale[2] < 0)
        {
            gltf->reason = "Mirrored instances require per-instance winding support";
            status = BL_UNSUPPORTED;
        }
        l_composeAssetInstance(matrices + i * 16, node.translation, node.rotation, node.scale);
    }
    if (status == BL_OK)
    {
        status = bl_setThinInstances(mesh, {matrices, count * 16}, count);
    }
    l_free(gltf->runtime, matrices);
    if (status == BL_OK)
    {
        status = bl_enableThinInstanceWorldBounds(mesh);
    }
    return status;
}

bl_Status l_gltfMesh(L_Gltf* gltf, L_Asset* asset, const cgltf_node* node,
                     const cgltf_primitive* primitive, bl_TransformNode parent)
{
    bl_Runtime* runtime = gltf->runtime;
    if (primitive->targets_count || primitive->has_draco_mesh_compression ||
        primitive->extensions_count || primitive->mappings_count)
    {
        gltf->reason =
            "Morph targets, compressed geometry and primitive extensions are unsupported";
        return BL_UNSUPPORTED;
    }
    bl_MeshGeometry geometry = {};
    bl_Status status = BL_OK;
    size_t vertices = 0;
    bool normal = false;
    for (size_t i = 0; i < primitive->attributes_count && status == BL_OK; ++i)
    {
        const cgltf_attribute* attribute = primitive->attributes + i;
        bl_F32Span* output = NULL;
        size_t components = 3;
        if (attribute->type == cgltf_attribute_type_position && !attribute->index)
        {
            output = &geometry.positions;
            if (!attribute->data->has_min || !attribute->data->has_max)
            {
                status = BL_INVALID_ARGUMENT;
            }
        }
        else if (attribute->type == cgltf_attribute_type_normal && !attribute->index)
        {
            output = &geometry.normals;
            normal = true;
        }
        else if (attribute->type == cgltf_attribute_type_tangent && !attribute->index)
        {
            output = &geometry.tangents;
            components = 4;
        }
        else if (attribute->type == cgltf_attribute_type_color && !attribute->index)
        {
            output = &geometry.colors;
            components = 4;
        }
        else if (attribute->type == cgltf_attribute_type_texcoord &&
                 (attribute->index == 0 || attribute->index == 1))
        {
            output = attribute->index ? &geometry.uvs2 : &geometry.uvs;
            components = 2;
        }
        else if (attribute->type == cgltf_attribute_type_joints ||
                 attribute->type == cgltf_attribute_type_weights)
        {
            continue;
        }
        else
        {
            status = BL_UNSUPPORTED;
        }
        if (output && status == BL_OK)
        {
            if (output->data || (vertices && vertices != attribute->data->count))
            {
                status = BL_INVALID_ARGUMENT;
            }
            else
            {
                status = stream(runtime, attribute->data, components, output == &geometry.colors,
                                output);
                vertices = attribute->data->count;
            }
        }
    }
    if (status == BL_OK && !geometry.positions.data)
    {
        status = BL_INVALID_ARGUMENT;
    }
    if (status == BL_OK && primitive->material)
    {
        const cgltf_material* material = primitive->material;
        const cgltf_texture_view* views[5] = {
            material->has_pbr_specular_glossiness
                ? &material->pbr_specular_glossiness.diffuse_texture
                : &material->pbr_metallic_roughness.base_color_texture,
            material->has_pbr_specular_glossiness
                ? &material->pbr_specular_glossiness.specular_glossiness_texture
                : &material->pbr_metallic_roughness.metallic_roughness_texture,
            &material->normal_texture, &material->occlusion_texture, &material->emissive_texture};
        for (size_t i = 0; i < 5; ++i)
        {
            if (views[i]->texture &&
                (views[i]->texcoord == 0 ? !geometry.uvs.data : !geometry.uvs2.data))
            {
                gltf->reason = "Material texture requires a missing UV channel";
                status = BL_UNSUPPORTED;
            }
        }
    }
    if (status == BL_OK && !geometry.uvs.data)
    {
        float* values = (float*)l_alloc(runtime, vertices * sizeof(float) * 2);
        if (!values)
        {
            status = BL_OUT_OF_MEMORY;
        }
        else
        {
            geometry.uvs = {values, vertices * 2};
        }
    }
    if (status == BL_OK)
    {
        status = indices(runtime, primitive, vertices, &geometry.indices);
    }
    if (status == BL_OK && !normal)
    {
        status = l_generateAssetNormals(runtime, &geometry);
    }
    bl_Mesh mesh = {};
    if (status == BL_OK)
    {
        const char* name = node->mesh->name ? node->mesh->name : "gltf_mesh";
        status = bl_createMeshFromData({runtime, asset->engine->record.id}, {name, strlen(name)},
                                       &geometry, &mesh);
    }
    l_free(runtime, (void*)geometry.positions.data);
    l_free(runtime, (void*)geometry.normals.data);
    l_free(runtime, (void*)geometry.uvs.data);
    l_free(runtime, (void*)geometry.uvs2.data);
    l_free(runtime, (void*)geometry.tangents.data);
    l_free(runtime, (void*)geometry.colors.data);
    l_free(runtime, (void*)geometry.indices.data);
    if (status != BL_OK)
    {
        return status;
    }
    ((L_Mesh*)l_peek(runtime, mesh._id))->assetFlatNormal = !normal;
    L_TRY(l_appendAssetNode(asset, {runtime, mesh._id}));
    bl_Material material = {};
    L_TRY(l_gltfMaterial(gltf, asset, primitive->material, &material));
    bl_MeshProperties2 properties = {material, {}, false};
    L_TRY(bl_setMeshProperties2(mesh, &properties));
    L_TRY(l_gltfBindSkin(gltf, asset, node, primitive, (L_Mesh*)l_peek(runtime, mesh._id)));
    if (node->skin)
    {
        parent = asset->root;
    }
    L_TRY(instances(gltf, node, mesh));
    L_TRY(bl_appendNodeChild(parent, {runtime, mesh._id}));
    return bl_setNodeParent({runtime, mesh._id}, parent);
}
