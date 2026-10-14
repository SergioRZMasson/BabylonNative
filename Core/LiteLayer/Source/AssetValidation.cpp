#include "AssetInternal.h"

static bl_Status unsupported(const char** reason, const char* message)
{
    if (reason)
    {
        *reason = message;
    }
    return BL_UNSUPPORTED;
}

static bl_Status accessor(const bl_ParsedGltf* data, uint32_t index, bl_AssetAccessorShape shape,
                          bool indices, size_t* count, bool normalizedAttribute = false,
                          const bl_ParsedGltf4* full = NULL)
{
    if (index >= data->accessorCount)
    {
        return BL_INVALID_ARGUMENT;
    }
    const bl_AssetAccessor* input = data->accessors + index;
    if (input->shape != shape || (input->sparsePresent && !full))
    {
        return input->shape != shape ? BL_INVALID_ARGUMENT : BL_UNSUPPORTED;
    }
    bool normalized = (!full || full->accessorInputs[index].hasNormalized) && input->normalized;
    bool integerUv = normalizedAttribute && normalized &&
                     (input->componentType == BL_ASSET_COMPONENT_U8 ||
                      input->componentType == BL_ASSET_COMPONENT_U16);
    if ((!indices && input->componentType != BL_ASSET_COMPONENT_F32 && !integerUv) ||
        (indices && input->componentType != BL_ASSET_COMPONENT_U8 &&
         input->componentType != BL_ASSET_COMPONENT_U16 &&
         input->componentType != BL_ASSET_COMPONENT_U32))
    {
        return BL_UNSUPPORTED;
    }
    if (normalized && !integerUv)
    {
        return BL_UNSUPPORTED;
    }
    if (!full && !input->bufferView.present)
    {
        return BL_UNSUPPORTED;
    }
    L_AssetAccessorReader reader;
    L_TRY(l_assetAccessorReader(data, index, full, &reader));
    if (input->bufferView.present)
    {
        const bl_AssetBufferView* view = data->bufferViews + input->bufferView.value;
        size_t offset = !full || full->accessorInputs[index].hasByteOffset ? input->byteOffset : 0;
        if ((indices && view->byteStride.present) ||
            (!indices && (offset % 4 || reader.stride % 4)))
        {
            return BL_INVALID_ARGUMENT;
        }
    }
    *count = input->count;
    return BL_OK;
}

static bool assetSpan(const void* pointer, size_t count, size_t size, size_t alignment)
{
    size_t bytes;
    return count <= UINT32_MAX && l_size(count, size, &bytes) && l_allocationFits(bytes) &&
           l_typedSpan(pointer, count, alignment);
}

bl_Status l_validateAsset(const bl_ParsedGltf* data, const char** reason,
                          const bl_ParsedGltf3* extended, const bl_ParsedGltf4* full)
{
    size_t expectedSize = full ? sizeof(*full) : extended ? sizeof(*extended) : sizeof(*data);
    uint32_t expectedVersion = full       ? BL_ASSET_DOCUMENT_VERSION_4
                               : extended ? BL_ASSET_DOCUMENT_VERSION_3
                                          : BL_ASSET_DOCUMENT_VERSION;
    if ((extended && &extended->document != data) || data->structSize != expectedSize ||
        (full && (extended != &full->document3 || (uintptr_t)full % alignof(bl_ParsedGltf4))) ||
        data->version != expectedVersion || data->gltfVersion != 2 ||
        !assetSpan(data->buffers, data->bufferCount, sizeof(bl_AssetBuffer),
                   alignof(bl_AssetBuffer)) ||
        !assetSpan(data->bufferViews, data->bufferViewCount, sizeof(bl_AssetBufferView),
                   alignof(bl_AssetBufferView)) ||
        !assetSpan(data->accessors, data->accessorCount, sizeof(bl_AssetAccessor),
                   alignof(bl_AssetAccessor)) ||
        !assetSpan(data->meshes, data->meshCount, sizeof(bl_AssetMesh), alignof(bl_AssetMesh)) ||
        !assetSpan(data->nodes, data->nodeCount, sizeof(bl_AssetNode), alignof(bl_AssetNode)) ||
        !assetSpan(data->materials, data->materialCount, sizeof(bl_AssetPbrDefinition),
                   alignof(bl_AssetPbrDefinition)) ||
        !assetSpan(data->scenes, data->sceneCount, sizeof(bl_AssetScene), alignof(bl_AssetScene)) ||
        !assetSpan(data->extensionsUsed, data->extensionsUsedCount, sizeof(bl_String),
                   alignof(bl_String)) ||
        !assetSpan(data->extensionsRequired, data->extensionsRequiredCount, sizeof(bl_String),
                   alignof(bl_String)))
    {
        return BL_INVALID_ARGUMENT;
    }
    if (full && (!assetSpan(full->accessorInputs, data->accessorCount,
                            sizeof(bl_AssetAccessorInputs), alignof(bl_AssetAccessorInputs)) ||
                 !assetSpan(full->bufferViewInputs, data->bufferViewCount,
                            sizeof(bl_AssetBufferViewInputs), alignof(bl_AssetBufferViewInputs))))
    {
        return BL_INVALID_ARGUMENT;
    }
    if (data->unsupportedFeatures & ~UINT64_C(15))
    {
        return BL_INVALID_ARGUMENT;
    }
    if (data->unsupportedFeatures || data->bufferCount != 1)
    {
        return unsupported(reason,
                           "Use a self-contained GLB2 with one BIN buffer and no unsupported "
                           "extensions or external buffer URIs");
    }
    if (data->animationCount || data->skinCount)
    {
        return unsupported(reason, "Skins and animation playback are not supported by the "
                                   "initial static-model loader");
    }
    if (!extended && (data->imageCount || data->textureCount || data->samplerCount))
    {
        return unsupported(reason, "This raw decoder service does not yet provide embedded "
                                   "image, texture and sampler descriptors");
    }
    if (data->cameraCount > UINT32_MAX)
    {
        return BL_INVALID_ARGUMENT;
    }
    bool instancingUsed = false;
    for (size_t i = 0; i < data->extensionsUsedCount; ++i)
    {
        if (!l_string(data->extensionsUsed[i]))
        {
            return BL_INVALID_ARGUMENT;
        }
        if (!l_name(data->extensionsUsed[i], "EXT_mesh_gpu_instancing"))
        {
            return unsupported(reason, "Unsupported extensionsUsed entry; only "
                                       "EXT_mesh_gpu_instancing is currently implemented");
        }
        instancingUsed = true;
    }
    for (size_t i = 0; i < data->extensionsRequiredCount; ++i)
    {
        if (!l_string(data->extensionsRequired[i]))
        {
            return BL_INVALID_ARGUMENT;
        }
        if (!l_name(data->extensionsRequired[i], "EXT_mesh_gpu_instancing") || !instancingUsed)
        {
            return unsupported(reason, "Unsupported extensionsRequired entry");
        }
    }
    const bl_AssetBuffer* buffer = data->buffers;
    if (!assetSpan(buffer->bytes.data, buffer->bytes.count, 1, 1) || !l_string(buffer->uri) ||
        buffer->declaredByteLength > buffer->bytes.count ||
        (uintptr_t)buffer->bytes.data > UINTPTR_MAX - buffer->declaredByteLength)
    {
        return BL_INVALID_ARGUMENT;
    }
    if (buffer->uri.length)
    {
        return unsupported(reason, "External/data buffer URIs are unsupported; use GLB BIN data");
    }
    for (size_t i = 0; i < data->bufferViewCount; ++i)
    {
        const bl_AssetBufferView* view = data->bufferViews + i;
        size_t offset = l_assetViewOffset(data, (uint32_t)i, full);
        if (view->buffer != 0 || offset > buffer->declaredByteLength ||
            view->byteLength > buffer->declaredByteLength - offset ||
            (full && full->bufferViewInputs[i].target.present &&
             full->bufferViewInputs[i].target.value != 34962 &&
             full->bufferViewInputs[i].target.value != 34963))
        {
            return BL_INVALID_ARGUMENT;
        }
    }
    for (size_t i = 0; i < data->accessorCount; ++i)
    {
        const bl_AssetAccessor* input = data->accessors + i;
        if ((unsigned)input->shape > BL_ASSET_MAT4 ||
            (input->componentType != BL_ASSET_COMPONENT_I8 &&
             input->componentType != BL_ASSET_COMPONENT_U8 &&
             input->componentType != BL_ASSET_COMPONENT_I16 &&
             input->componentType != BL_ASSET_COMPONENT_U16 &&
             input->componentType != BL_ASSET_COMPONENT_U32 &&
             input->componentType != BL_ASSET_COMPONENT_F32) ||
            (input->bufferView.present && input->bufferView.value >= data->bufferViewCount))
        {
            return BL_INVALID_ARGUMENT;
        }
        if (input->count > UINT32_MAX)
        {
            return BL_INVALID_ARGUMENT;
        }
        if (input->sparsePresent && !full)
        {
            return unsupported(reason, "Sparse accessors are not implemented");
        }
    }
    if (full)
    {
        L_TRY(l_validateAssetAccessorBounds(data, full));
    }
    for (size_t i = 0; i < data->materialCount; ++i)
    {
        const bl_AssetPbrDefinition* material = data->materials + i;
        if ((material->hasName && !l_string(material->name)) ||
            (material->unsupportedFeatures & ~UINT64_C(15)) ||
            (material->textureInfoFields & ~UINT32_C(31)) ||
            (material->hasAlphaMode && (unsigned)material->alphaMode > BL_ASSET_ALPHA_BLEND))
        {
            return BL_INVALID_ARGUMENT;
        }
        if ((material->hasMetallicFactor && !isfinite(material->metallicFactor)) ||
            (material->hasRoughnessFactor && !isfinite(material->roughnessFactor)) ||
            (material->hasEmissiveFactor && !l_vec(material->emissiveFactor)) ||
            (material->hasAlphaCutoff &&
             (!isfinite(material->alphaCutoff) || material->alphaCutoff < 0)) ||
            (material->hasBaseColorFactor &&
             (!isfinite(material->baseColorFactor.r) || !isfinite(material->baseColorFactor.g) ||
              !isfinite(material->baseColorFactor.b) || !isfinite(material->baseColorFactor.a) ||
              material->baseColorFactor.a < 0 || material->baseColorFactor.a > 1)))
        {
            return BL_INVALID_ARGUMENT;
        }
        if (material->unsupportedFeatures || (!extended && material->textureInfoFields))
        {
            return unsupported(reason, "Material requires unsupported texture inputs "
                                       "or material extension");
        }
    }
    if (extended)
    {
        L_TRY(l_validateAssetTextures(extended, reason));
    }
    for (size_t i = 0; i < data->meshCount; ++i)
    {
        const bl_AssetMesh* mesh = data->meshes + i;
        if ((mesh->hasName && !l_string(mesh->name)) ||
            !assetSpan(mesh->primitives, mesh->primitiveCount, sizeof(bl_AssetPrimitive),
                       alignof(bl_AssetPrimitive)))
        {
            return BL_INVALID_ARGUMENT;
        }
        for (size_t j = 0; j < mesh->primitiveCount; ++j)
        {
            const bl_AssetPrimitive* primitive = mesh->primitives + j;
            if (!assetSpan(primitive->attributes, primitive->attributeCount,
                           sizeof(bl_AssetAttribute), alignof(bl_AssetAttribute)) ||
                (primitive->material.present && primitive->material.value >= data->materialCount) ||
                (primitive->unsupportedFeatures & ~UINT64_C(15)) ||
                (primitive->hasMode && (unsigned)primitive->mode > BL_ASSET_MODE_TRIANGLE_FAN))
            {
                return BL_INVALID_ARGUMENT;
            }
            if (primitive->unsupportedFeatures || primitive->morphTargetCount ||
                (primitive->hasMode && primitive->mode != BL_ASSET_MODE_TRIANGLES &&
                 primitive->mode != BL_ASSET_MODE_TRIANGLE_STRIP &&
                 primitive->mode != BL_ASSET_MODE_TRIANGLE_FAN))
            {
                return unsupported(reason,
                                   "Only static triangle lists, strips and fans are implemented; "
                                   "morph targets and point/line modes are unsupported");
            }
            size_t indexCount = 0;
            if (primitive->indices.present)
            {
                L_TRY(accessor(data, primitive->indices.value, BL_ASSET_SCALAR, true, &indexCount,
                               false, full));
                if ((!primitive->hasMode || primitive->mode == BL_ASSET_MODE_TRIANGLES)
                        ? indexCount % 3 != 0
                        : indexCount < 3)
                {
                    return BL_INVALID_ARGUMENT;
                }
            }
            size_t vertices = SIZE_MAX;
            uint32_t uvChannels = 0;
            for (size_t k = 0; k < primitive->attributeCount; ++k)
            {
                const bl_AssetAttribute* attribute = primitive->attributes + k;
                if (!l_string(attribute->semantic))
                {
                    return BL_INVALID_ARGUMENT;
                }
                for (size_t previous = 0; previous < k; ++previous)
                {
                    if (l_equal(attribute->semantic, primitive->attributes[previous].semantic))
                    {
                        return BL_INVALID_ARGUMENT;
                    }
                }
                bl_AssetAccessorShape shape = BL_ASSET_VEC3;
                if (l_name(attribute->semantic, "TEXCOORD_0") ||
                    l_name(attribute->semantic, "TEXCOORD_1"))
                {
                    shape = BL_ASSET_VEC2;
                    uvChannels |= l_name(attribute->semantic, "TEXCOORD_0") ? 1u : 2u;
                }
                else if (l_name(attribute->semantic, "TANGENT"))
                {
                    shape = BL_ASSET_VEC4;
                }
                else if (l_name(attribute->semantic, "COLOR_0"))
                {
                    if (attribute->accessor >= data->accessorCount)
                    {
                        return BL_INVALID_ARGUMENT;
                    }
                    shape = data->accessors[attribute->accessor].shape;
                    if (shape != BL_ASSET_VEC3 && shape != BL_ASSET_VEC4)
                    {
                        return BL_INVALID_ARGUMENT;
                    }
                    const bl_AssetAccessor* color = data->accessors + attribute->accessor;
                    bool normalized =
                        (!full || full->accessorInputs[attribute->accessor].hasNormalized) &&
                        color->normalized;
                    if (color->componentType == BL_ASSET_COMPONENT_F32
                            ? normalized
                            : (color->componentType != BL_ASSET_COMPONENT_U8 &&
                               color->componentType != BL_ASSET_COMPONENT_U16) ||
                                  !normalized)
                    {
                        return BL_INVALID_ARGUMENT;
                    }
                }
                else if (!l_name(attribute->semantic, "POSITION") &&
                         !l_name(attribute->semantic, "NORMAL"))
                {
                    return unsupported(reason, "Unsupported vertex semantic; use POSITION, NORMAL, "
                                               "TEXCOORD_0/1, TANGENT and COLOR_0");
                }
                size_t count;
                L_TRY(accessor(data, attribute->accessor, shape, false, &count,
                               shape == BL_ASSET_VEC2 || l_name(attribute->semantic, "COLOR_0"),
                               full));
                if (l_name(attribute->semantic, "COLOR_0"))
                {
                    L_AssetAccessorReader reader;
                    L_TRY(l_assetAccessorReader(data, attribute->accessor, full, &reader));
                    for (size_t element = 0; element < count; ++element)
                    {
                        double values[4];
                        L_TRY(l_assetAccessorElement(&reader, element, values));
                        for (size_t component = 0; component < reader.rows; ++component)
                        {
                            if (!isfinite(values[component]) || values[component] < 0 ||
                                values[component] > 1)
                            {
                                return BL_INVALID_ARGUMENT;
                            }
                        }
                    }
                }
                if (full && l_name(attribute->semantic, "POSITION") &&
                    (!full->accessorInputs[attribute->accessor].hasMin ||
                     !full->accessorInputs[attribute->accessor].hasMax))
                {
                    return BL_INVALID_ARGUMENT;
                }
                if (vertices == SIZE_MAX)
                {
                    vertices = count;
                }
                else if (vertices != count)
                {
                    return BL_INVALID_ARGUMENT;
                }
            }
            bool position = false;
            for (size_t k = 0; k < primitive->attributeCount; ++k)
            {
                position |= l_name(primitive->attributes[k].semantic, "POSITION");
            }
            if (!position)
            {
                return unsupported(reason, "A triangle primitive needs POSITION data");
            }
            if (!primitive->indices.present &&
                ((!primitive->hasMode || primitive->mode == BL_ASSET_MODE_TRIANGLES)
                     ? vertices % 3 != 0
                     : vertices < 3))
            {
                return BL_INVALID_ARGUMENT;
            }
            if (extended && primitive->material.present)
            {
                const bl_AssetMaterialInputs* inputs =
                    extended->materialInputs + primitive->material.value;
                const bl_AssetTextureInfo* textures[5] = {
                    &inputs->baseColor, &inputs->metallicRoughness, &inputs->normal,
                    &inputs->occlusion, &inputs->emissive};
                for (unsigned k = 0; k < 5; ++k)
                {
                    uint32_t channel =
                        textures[k]->texCoord.present ? textures[k]->texCoord.value : 0;
                    if (textures[k]->present && !(uvChannels & (1u << channel)))
                    {
                        return unsupported(reason,
                                           "Material texture requires a missing UV channel");
                    }
                }
            }
        }
    }
    for (size_t i = 0; i < data->nodeCount; ++i)
    {
        const bl_AssetNode* node = data->nodes + i;
        if ((node->hasName && !l_string(node->name)) ||
            !assetSpan(node->children, node->childCount, sizeof(uint32_t), alignof(uint32_t)) ||
            (node->mesh.present && node->mesh.value >= data->meshCount) ||
            (node->camera.present && node->camera.value >= data->cameraCount) ||
            (node->unsupportedFeatures & ~UINT64_C(15)) ||
            (node->hasTranslation && !l_vec(node->translation)) ||
            (node->hasRotation && !l_quat(node->rotation)) ||
            (node->hasScale && !l_vec(node->scale)))
        {
            return BL_INVALID_ARGUMENT;
        }
        if (node->skin.present || node->unsupportedFeatures)
        {
            return unsupported(reason, "Node requires an unsupported skin or extension");
        }
        bl_TransformNodeOptions options;
        L_TRY(l_composeAssetMatrix(node, &options));
        for (size_t j = 0; j < node->childCount; ++j)
        {
            if (node->children[j] >= data->nodeCount)
            {
                return BL_INVALID_ARGUMENT;
            }
        }
        if (node->hasInstancing)
        {
            if (!instancingUsed)
            {
                return unsupported(reason, "Instancing data requires EXT_mesh_gpu_instancing");
            }
            const bl_AssetOptionalIndex* indices[3] = {
                &node->instanceTranslation, &node->instanceRotation, &node->instanceScale};
            size_t instances = SIZE_MAX;
            for (unsigned j = 0; j < 3; ++j)
            {
                if (indices[j]->present)
                {
                    size_t count;
                    L_TRY(accessor(data, indices[j]->value, j == 1 ? BL_ASSET_VEC4 : BL_ASSET_VEC3,
                                   false, &count, false, full));
                    if (instances != SIZE_MAX && instances != count)
                    {
                        return BL_INVALID_ARGUMENT;
                    }
                    instances = count;
                    L_AssetAccessorReader reader;
                    L_TRY(l_assetAccessorReader(data, indices[j]->value, full, &reader));
                    size_t components = j == 1 ? 4 : 3;
                    for (size_t k = 0; k < count; ++k)
                    {
                        double values[4] = {};
                        L_TRY(l_assetAccessorElement(&reader, k, values));
                        for (size_t component = 0; component < components; ++component)
                        {
                            if (!isfinite(values[component]))
                            {
                                return BL_INVALID_ARGUMENT;
                            }
                        }
                        if (j == 2 && (double)values[0] * values[1] * values[2] < 0)
                        {
                            return unsupported(reason,
                                               "Mirrored EXT_mesh_gpu_instancing scales need "
                                               "per-instance winding support; ordinary mirrored "
                                               "nodes and matrices are supported");
                        }
                    }
                }
            }
        }
    }
    for (size_t i = 0; i < data->sceneCount; ++i)
    {
        const bl_AssetScene* scene = data->scenes + i;
        if (!assetSpan(scene->roots, scene->rootCount, sizeof(uint32_t), alignof(uint32_t)))
        {
            return BL_INVALID_ARGUMENT;
        }
        for (size_t j = 0; j < scene->rootCount; ++j)
        {
            if (scene->roots[j] >= data->nodeCount)
            {
                return BL_INVALID_ARGUMENT;
            }
        }
    }
    if (data->defaultScene.present && data->defaultScene.value >= data->sceneCount)
    {
        return BL_INVALID_ARGUMENT;
    }
    return BL_OK;
}

bl_Status l_validateAssetHierarchy(bl_Runtime* runtime, const bl_ParsedGltf* data)
{
    size_t bytes;
    if (!l_size(data->nodeCount, sizeof(uint32_t), &bytes))
    {
        return BL_INVALID_ARGUMENT;
    }
    uint32_t* parents = (uint32_t*)l_alloc(runtime, bytes ? bytes : sizeof(uint32_t));
    uint32_t* queue = (uint32_t*)l_alloc(runtime, bytes ? bytes : sizeof(uint32_t));
    if (!parents || !queue)
    {
        l_free(runtime, parents);
        l_free(runtime, queue);
        return BL_OUT_OF_MEMORY;
    }
    bl_Status status = BL_OK;
    for (size_t i = 0; i < data->nodeCount && status == BL_OK; ++i)
    {
        const bl_AssetNode* node = data->nodes + i;
        for (size_t j = 0; j < node->childCount; ++j)
        {
            uint32_t child = node->children[j];
            if (child >= data->nodeCount || parents[child]++)
            {
                status = BL_INVALID_ARGUMENT;
                break;
            }
        }
    }
    for (size_t i = 0; i < data->sceneCount && status == BL_OK; ++i)
    {
        const bl_AssetScene* scene = data->scenes + i;
        memset(queue, 0, bytes);
        for (size_t j = 0; j < scene->rootCount; ++j)
        {
            uint32_t root = scene->roots[j];
            if (root >= data->nodeCount || parents[root] || queue[root]++)
            {
                status = BL_INVALID_ARGUMENT;
                break;
            }
        }
    }
    size_t count = 0;
    for (size_t i = 0; i < data->nodeCount; ++i)
    {
        if (!parents[i])
        {
            queue[count++] = (uint32_t)i;
        }
    }
    for (size_t first = 0; first < count && status == BL_OK; ++first)
    {
        const bl_AssetNode* node = data->nodes + queue[first];
        for (size_t j = 0; j < node->childCount; ++j)
        {
            uint32_t child = node->children[j];
            if (!--parents[child])
            {
                queue[count++] = child;
            }
        }
    }
    if (status == BL_OK && count != data->nodeCount)
    {
        status = BL_INVALID_ARGUMENT;
    }
    l_free(runtime, parents);
    l_free(runtime, queue);
    return status;
}
