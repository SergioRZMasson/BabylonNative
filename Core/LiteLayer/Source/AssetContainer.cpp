#include "AssetInternal.h"

static void cleanupAsset(bl_Runtime* runtime, L_Record* record)
{
    L_Asset* asset = (L_Asset*)record;
    for (size_t i = asset->nodeCount; i > 0; --i)
    {
        L_Record* member = l_peek(runtime, asset->nodes[i - 1]._id);
        if (!member)
        {
            continue;
        }
        L_Node* node = (L_Node*)member;
        if (!member->disposed)
        {
            l_parent(node, NULL);
            if (member->kind == L_MESH)
            {
                l_meshCleanup(runtime, member);
            }
            else
            {
                l_cleanNode(runtime, node);
                member->disposed = true;
            }
        }
        l_unpin(member);
    }
    for (size_t i = 0; i < asset->materialCount; ++i)
    {
        if (!asset->materials)
        {
            break;
        }
        L_Record* material = l_peek(runtime, asset->materials[i]._id);
        if (material)
        {
            l_unpin(material);
        }
    }
    for (size_t i = 0; i < asset->materialCount; ++i)
    {
        if (asset->materials && !L_NULL(asset->materials[i]))
        {
            bl_disposePbrMaterial({runtime, asset->materials[i]._id});
        }
    }
    for (size_t i = 0; i < asset->textureCount; ++i)
    {
        if (asset->textures && !L_NULL(asset->textures[i]))
        {
            bl_disposeTexture2D(asset->textures[i]);
        }
    }
    l_free(runtime, asset->nodes);
    l_free(runtime, asset->materials);
    l_free(runtime, asset->textures);
    asset->nodes = NULL;
    asset->materials = NULL;
    asset->textures = NULL;
    if (asset->engine)
    {
        l_unpin(&asset->engine->record);
    }
    asset->engine = NULL;
    record->disposed = true;
}

static bl_Status assetBusy(bl_Runtime*, L_Record* record)
{
    L_Asset* asset = (L_Asset*)record;
    for (size_t i = 0; i < asset->nodeCount; ++i)
    {
        L_Record* member = l_peek(asset->engine->runtime, asset->nodes[i]._id);
        if (member && !member->disposed && ((L_Node*)member)->sceneCount)
        {
            return BL_BUSY;
        }
    }
    return BL_OK;
}

bl_Status bl_getAssetContainerEntities(bl_AssetContainer handle, bl_AssetEntities* out)
{
    L_GET(handle, L_ASSET, L_Asset, asset);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = {&asset->root, 1};
    return BL_OK;
}

bl_Status bl_disposeAssetContainer(bl_AssetContainer handle)
{
    L_RETIRED(handle, L_ASSET, L_Asset, asset);
    if (!asset || asset->record.disposed)
    {
        return BL_OK;
    }
    L_TRY(assetBusy(handle._runtime, &asset->record));
    L_TRY(l_retire(asset->engine));
    cleanupAsset(handle._runtime, &asset->record);
    return BL_OK;
}

bl_Status bl_addAssetContainerToScene(bl_SceneContext sceneHandle, bl_AssetContainer handle)
{
    L_GET(sceneHandle, L_SCENE, L_Scene, scene);
    if (handle._runtime != sceneHandle._runtime)
    {
        return BL_WRONG_RUNTIME;
    }
    L_GET(handle, L_ASSET, L_Asset, asset);
    if (asset->engine != scene->engine)
    {
        return BL_WRONG_ENGINE;
    }
    L_Node* root = NULL;
    L_TRY(l_node(asset->root, &root));
    return l_addAssetHierarchy(sceneHandle._runtime, scene, root);
}

bl_Status bl_removeAssetContainerFromScene(bl_SceneContext sceneHandle, bl_AssetContainer handle)
{
    L_GET(sceneHandle, L_SCENE, L_Scene, scene);
    if (handle._runtime != sceneHandle._runtime)
    {
        return BL_WRONG_RUNTIME;
    }
    L_GET(handle, L_ASSET, L_Asset, asset);
    if (asset->engine != scene->engine)
    {
        return BL_WRONG_ENGINE;
    }
    return bl_removeFromScene(sceneHandle, asset->root);
}

static bl_Status appendAssetNode(L_Asset* asset, bl_SceneNode node)
{
    if (asset->nodeCount == asset->nodeCapacity)
    {
        return BL_OUT_OF_MEMORY;
    }
    asset->nodes[asset->nodeCount++] = node;
    l_pin(l_peek(node._runtime, node._id));
    return BL_OK;
}

static void composeInstance(float* out, const float* translation, const float* rotation,
                            const float* scale)
{
    double tx = translation ? translation[0] : 0;
    double ty = translation ? translation[1] : 0;
    double tz = translation ? translation[2] : 0;
    double x = rotation ? rotation[0] : 0;
    double y = rotation ? rotation[1] : 0;
    double z = rotation ? rotation[2] : 0;
    double w = rotation ? rotation[3] : 1;
    double sx = scale ? scale[0] : 1;
    double sy = scale ? scale[1] : 1;
    double sz = scale ? scale[2] : 1;
    double xx = x * x;
    double yy = y * y;
    double zz = z * z;
    double xy = x * y;
    double xz = x * z;
    double yz = y * z;
    double wx = w * x;
    double wy = w * y;
    double wz = w * z;
    out[0] = (float)((1 - 2 * (yy + zz)) * sx);
    out[1] = (float)(2 * (xy + wz) * sx);
    out[2] = (float)(2 * (xz - wy) * sx);
    out[3] = 0;
    out[4] = (float)(2 * (xy - wz) * sy);
    out[5] = (float)((1 - 2 * (xx + zz)) * sy);
    out[6] = (float)(2 * (yz + wx) * sy);
    out[7] = 0;
    out[8] = (float)(2 * (xz + wy) * sz);
    out[9] = (float)(2 * (yz - wx) * sz);
    out[10] = (float)((1 - 2 * (xx + yy)) * sz);
    out[11] = 0;
    out[12] = (float)tx;
    out[13] = (float)ty;
    out[14] = (float)tz;
    out[15] = 1;
}

static bl_Status setInstances(bl_Mesh mesh, const bl_ParsedGltf* data, const bl_AssetNode* node,
                              const bl_ParsedGltf4* full)
{
    if (!node->hasInstancing)
    {
        return BL_OK;
    }
    const bl_AssetOptionalIndex* accessors[3] = {&node->instanceTranslation,
                                                 &node->instanceRotation, &node->instanceScale};
    L_AssetAccessorReader readers[3] = {};
    size_t count = 0;
    for (unsigned i = 0; i < 3; ++i)
    {
        if (accessors[i]->present)
        {
            const bl_AssetAccessor* input = data->accessors + accessors[i]->value;
            L_TRY(l_assetAccessorReader(data, accessors[i]->value, full, readers + i));
            if (count && count != input->count)
            {
                return l_error(mesh._runtime, BL_INVALID_ARGUMENT, __func__,
                               "EXT_mesh_gpu_instancing accessor count mismatch", 155);
            }
            count = input->count;
        }
    }
    if (!count)
    {
        return BL_OK;
    }
    size_t bytes;
    if (!l_size(count, 64, &bytes) || bytes > UINT32_MAX)
    {
        return BL_INVALID_ARGUMENT;
    }
    float* matrices = (float*)l_alloc(mesh._runtime, bytes);
    if (!matrices)
    {
        return BL_OUT_OF_MEMORY;
    }
    for (size_t i = 0; i < count; ++i)
    {
        float values[3][4] = {};
        for (unsigned j = 0; j < 3; ++j)
        {
            if (accessors[j]->present)
            {
                double components[4] = {};
                l_assetAccessorElement(readers + j, i, components);
                for (unsigned k = 0; k < (j == 1 ? 4u : 3u); ++k)
                {
                    values[j][k] = (float)components[k];
                }
            }
        }
        composeInstance(matrices + i * 16, accessors[0]->present ? values[0] : NULL,
                        accessors[1]->present ? values[1] : NULL,
                        accessors[2]->present ? values[2] : NULL);
    }
    bl_Status status = bl_setThinInstances(mesh, {matrices, count * 16}, count);
    l_free(mesh._runtime, matrices);
    if (status == BL_OK)
    {
        status = bl_enableThinInstanceWorldBounds(mesh);
    }
    return status;
}

static bl_Status copyAccessor(bl_Runtime* runtime, const bl_ParsedGltf* data, uint32_t index,
                              bl_F32Span* out, const bl_ParsedGltf4* full, bool color = false)
{
    const bl_AssetAccessor* accessor = data->accessors + index;
    L_AssetAccessorReader reader;
    L_TRY(l_assetAccessorReader(data, index, full, &reader));
    size_t sourceComponents = reader.rows * reader.columns;
    size_t components = color ? 4 : sourceComponents;
    size_t count;
    size_t bytes;
    if (!l_size(accessor->count, components, &count) || !l_size(count, sizeof(float), &bytes) ||
        bytes > UINT32_MAX)
    {
        return BL_INVALID_ARGUMENT;
    }
    float* values = (float*)l_alloc(runtime, bytes ? bytes : sizeof(float));
    if (!values)
    {
        return BL_OUT_OF_MEMORY;
    }
    for (size_t i = 0; i < accessor->count; ++i)
    {
        double element[16];
        l_assetAccessorElement(&reader, i, element);
        for (size_t j = 0; j < components; ++j)
        {
            double value = j < sourceComponents ? element[j] : 1;
            if (!isfinite(value) || !isfinite((float)value) || (color && (value < 0 || value > 1)))
            {
                l_free(runtime, values);
                return BL_INVALID_ARGUMENT;
            }
            values[i * components + j] = (float)value;
        }
    }
    *out = {values, count};
    return BL_OK;
}

static bl_Status generateAssetNormals(bl_Runtime* runtime, bl_MeshGeometry* geometry)
{
    size_t bytes;
    if (!l_size(geometry->positions.count, sizeof(float), &bytes) || bytes > UINT32_MAX)
    {
        return BL_INVALID_ARGUMENT;
    }
    float* normals = (float*)l_alloc(runtime, bytes ? bytes : sizeof(float));
    if (!normals)
    {
        return BL_OUT_OF_MEMORY;
    }
    memset(normals, 0, bytes);
    geometry->normals = {normals, geometry->positions.count};
    for (size_t i = 0; i < geometry->indices.count; i += 3)
    {
        uint32_t a = geometry->indices.data[i];
        uint32_t b = geometry->indices.data[i + 1];
        uint32_t c = geometry->indices.data[i + 2];
        if (a >= geometry->positions.count / 3 || b >= geometry->positions.count / 3 ||
            c >= geometry->positions.count / 3)
        {
            return BL_INVALID_ARGUMENT;
        }
        const float* positions = geometry->positions.data;
        double ux = (double)positions[b * 3] - positions[a * 3];
        double uy = (double)positions[b * 3 + 1] - positions[a * 3 + 1];
        double uz = (double)positions[b * 3 + 2] - positions[a * 3 + 2];
        double vx = (double)positions[c * 3] - positions[a * 3];
        double vy = (double)positions[c * 3 + 1] - positions[a * 3 + 1];
        double vz = (double)positions[c * 3 + 2] - positions[a * 3 + 2];
        double nx = uy * vz - uz * vy;
        double ny = uz * vx - ux * vz;
        double nz = ux * vy - uy * vx;
        const uint32_t vertices[3] = {a, b, c};
        for (unsigned j = 0; j < 3; ++j)
        {
            size_t offset = (size_t)vertices[j] * 3;
            normals[offset] = (float)(normals[offset] + nx);
            normals[offset + 1] = (float)(normals[offset + 1] + ny);
            normals[offset + 2] = (float)(normals[offset + 2] + nz);
        }
    }
    for (size_t i = 0; i < geometry->normals.count; i += 3)
    {
        double length =
            sqrt((double)normals[i] * normals[i] + (double)normals[i + 1] * normals[i + 1] +
                 (double)normals[i + 2] * normals[i + 2]);
        if (length > 0)
        {
            normals[i] = (float)(normals[i] / length);
            normals[i + 1] = (float)(normals[i + 1] / length);
            normals[i + 2] = (float)(normals[i + 2] / length);
        }
    }
    return BL_OK;
}

static bl_Status buildMesh(L_Asset* asset, const bl_ParsedGltf* data, uint32_t nodeIndex,
                           const bl_AssetPrimitive* primitive, bl_TransformNode parent,
                           const bl_ParsedGltf3* extended, const bl_ParsedGltf4* full,
                           const bl_ParsedGltf5* materials)
{
    bl_Runtime* runtime = asset->engine->runtime;
    bl_MeshGeometry geometry = {};
    size_t vertices = 0;
    bool authoredNormal = false;
    bl_Status status = BL_OK;
    for (size_t i = 0; i < primitive->attributeCount && status == BL_OK; ++i)
    {
        const bl_AssetAttribute* input = primitive->attributes + i;
        const bl_AssetAccessor* descriptor = data->accessors + input->accessor;
        authoredNormal |= l_name(input->semantic, "NORMAL");
        bl_F32Span* stream = l_name(input->semantic, "POSITION")     ? &geometry.positions
                             : l_name(input->semantic, "NORMAL")     ? &geometry.normals
                             : l_name(input->semantic, "TEXCOORD_0") ? &geometry.uvs
                             : l_name(input->semantic, "TEXCOORD_1") ? &geometry.uvs2
                             : l_name(input->semantic, "COLOR_0")    ? &geometry.colors
                                                                     : &geometry.tangents;
        status =
            copyAccessor(runtime, data, input->accessor, stream, full, stream == &geometry.colors);
        vertices = descriptor->count;
    }
    if (status == BL_OK && !geometry.uvs.data)
    {
        float* uvs = (float*)l_alloc(runtime, vertices ? vertices * 8 : sizeof(float));
        if (!uvs)
        {
            status = BL_OUT_OF_MEMORY;
        }
        else
        {
            memset(uvs, 0, vertices * 8);
            geometry.uvs = {uvs, vertices * 2};
        }
    }
    if (status == BL_OK)
    {
        status =
            l_assetTriangleIndices(runtime, data, primitive, vertices, full, &geometry.indices);
    }
    if (status == BL_OK && !geometry.normals.data)
    {
        status = generateAssetNormals(runtime, &geometry);
    }
    const bl_AssetMesh* sourceMesh = data->meshes + data->nodes[nodeIndex].mesh.value;
    char fallback[64];
    snprintf(fallback, sizeof(fallback), "gltf_mesh_%zu", asset->nodeCount);
    bl_String name = sourceMesh->hasName && sourceMesh->name.length
                         ? sourceMesh->name
                         : bl_String{fallback, strlen(fallback)};
    bl_Mesh mesh = {};
    if (status == BL_OK)
    {
        status = bl_createMeshFromData({runtime, asset->engine->record.id}, name, &geometry, &mesh);
    }
    l_free(runtime, (void*)geometry.indices.data);
    l_free(runtime, (void*)geometry.positions.data);
    l_free(runtime, (void*)geometry.normals.data);
    l_free(runtime, (void*)geometry.uvs.data);
    l_free(runtime, (void*)geometry.uvs2.data);
    l_free(runtime, (void*)geometry.tangents.data);
    l_free(runtime, (void*)geometry.colors.data);
    if (status != BL_OK)
    {
        return status;
    }
    L_Mesh* nativeMesh = (L_Mesh*)l_peek(runtime, mesh._id);
    nativeMesh->assetFlatNormal = !authoredNormal;
    L_TRY(appendAssetNode(asset, {runtime, mesh._id}));
    size_t materialIndex = primitive->material.present ? primitive->material.value + 1 : 0;
    if (L_NULL(asset->materials[materialIndex]))
    {
        bl_Material material = {};
        const bl_AssetSpecularGlossinessInputs* inputs =
            materials && primitive->material.present
                ? materials->specularGlossinessInputs + primitive->material.value
                : NULL;
        L_TRY(l_createPbrFactorMaterial(
            {runtime, asset->engine->record.id},
            primitive->material.present ? data->materials + primitive->material.value : NULL,
            &material, inputs));
        asset->materials[materialIndex] = material;
        l_pin(l_peek(runtime, material._id));
        if (extended && primitive->material.present)
        {
            L_TRY(l_applyAssetMaterialInputs(asset, extended, primitive->material.value, material,
                                             full, inputs));
        }
    }
    bl_MeshProperties2 properties = {asset->materials[materialIndex], {}, false};
    L_TRY(bl_setMeshProperties2(mesh, &properties));
    L_TRY(setInstances(mesh, data, data->nodes + nodeIndex, full));
    L_TRY(bl_appendNodeChild(parent, {runtime, mesh._id}));
    return bl_setNodeParent({runtime, mesh._id}, parent);
}

static bl_Status createAssetNode(L_Asset* asset, const bl_ParsedGltf* data, uint32_t index,
                                 bl_TransformNode parent, bl_TransformNode* out)
{
    bl_Runtime* runtime = asset->engine->runtime;
    const bl_AssetNode* source = data->nodes + index;
    bl_TransformNodeOptions options;
    L_TRY(l_composeAssetMatrix(source, &options));
    char fallback[64];
    snprintf(fallback, sizeof(fallback), "node_%u", index);
    bl_String name = source->hasName ? source->name : bl_String{fallback, strlen(fallback)};
    bl_TransformNode node = {};
    L_TRY(bl_createTransformNode(runtime, name, &options, &node));
    if (source->hasMatrix)
    {
        L_Node* record = (L_Node*)l_peek(runtime, node._id);
        record->localMatrixLocked = true;
        record->lockedLocalMatrix = source->matrix;
    }
    L_TRY(appendAssetNode(asset, node));
    L_TRY(bl_appendNodeChild(parent, node));
    L_TRY(bl_setNodeParent(node, parent));
    *out = node;
    return BL_OK;
}

struct L_AssetBuildFrame
{
    uint32_t index;
    bl_TransformNode node;
    size_t nextChild;
};

static bl_Status buildNode(L_Asset* asset, const bl_ParsedGltf* data, uint32_t index,
                           bl_TransformNode parent, const bl_ParsedGltf3* extended,
                           const bl_ParsedGltf4* full, const bl_ParsedGltf5* materials)
{
    bl_Runtime* runtime = asset->engine->runtime;
    size_t bytes;
    if (!l_size(data->nodeCount, sizeof(L_AssetBuildFrame), &bytes))
    {
        return BL_INVALID_ARGUMENT;
    }
    L_AssetBuildFrame* stack = (L_AssetBuildFrame*)l_alloc(runtime, bytes);
    if (!stack)
    {
        return BL_OUT_OF_MEMORY;
    }
    size_t depth = 1;
    stack[0].index = index;
    bl_Status status = createAssetNode(asset, data, index, parent, &stack[0].node);
    while (depth && status == BL_OK)
    {
        L_AssetBuildFrame* frame = stack + depth - 1;
        const bl_AssetNode* source = data->nodes + frame->index;
        if (frame->nextChild < source->childCount)
        {
            uint32_t child = source->children[frame->nextChild++];
            L_AssetBuildFrame* next = stack + depth++;
            next->index = child;
            next->nextChild = 0;
            status = createAssetNode(asset, data, child, frame->node, &next->node);
            continue;
        }
        if (source->mesh.present)
        {
            const bl_AssetMesh* mesh = data->meshes + source->mesh.value;
            for (size_t i = 0; i < mesh->primitiveCount && status == BL_OK; ++i)
            {
                status = buildMesh(asset, data, frame->index, mesh->primitives + i, frame->node,
                                   extended, full, materials);
            }
        }
        --depth;
    }
    l_free(runtime, stack);
    return status;
}

bl_Status bl_loadGltf(bl_EngineContext handle, const bl_AssetSource* source, bl_AssetContainer* out)
{
    L_GET(handle, L_ENGINE, L_Engine, engine);
    bl_Runtime* runtime = handle._runtime;
    if (l_inFrame() || l_inDispatch() || engine->inFrame)
    {
        return BL_BUSY;
    }
    if (!source || !out || (unsigned)source->kind > BL_ASSET_SOURCE_BYTES ||
        !l_string(source->uri) || !l_span(source->bytes.data, source->bytes.count) ||
        (source->kind == BL_ASSET_SOURCE_URI && (!source->uri.length || source->bytes.count)) ||
        (source->kind == BL_ASSET_SOURCE_BYTES && (source->uri.length || !source->bytes.count)))
    {
        return BL_INVALID_ARGUMENT;
    }
    if (!runtime->hasAssets)
    {
        return BL_UNSUPPORTED;
    }
    bl_HostBuffer buffer = {};
    bool read = source->kind == BL_ASSET_SOURCE_URI;
    bl_Bytes input = source->bytes;
    bl_Status status = BL_OK;
    if (read)
    {
        if (!runtime->hasIO || !runtime->io.readFile || !runtime->io.releaseBuffer)
        {
            return BL_UNSUPPORTED;
        }
        L_CALL(runtime, status, runtime->io.readFile(runtime->io.userData, source->uri, &buffer));
        input = {buffer.data, buffer.byteCount};
    }
    bl_ParsedGltf legacy = {};
    bl_ParsedGltf3 extended = {};
    bl_ParsedGltf4 full = {};
    bl_ParsedGltf5 materialData = {};
    bool version3 = runtime->assets.version == BL_ASSET_SERVICE_VERSION_3;
    bool version4 = runtime->assets.version == BL_ASSET_SERVICE_VERSION_4;
    bool version5 = runtime->assets.version == BL_ASSET_SERVICE_VERSION_5;
    bl_ParsedGltf4* accessors = version5 ? &materialData.document4 : version4 ? &full : NULL;
    bl_ParsedGltf3* textures = accessors ? &accessors->document3 : version3 ? &extended : NULL;
    bl_ParsedGltf& parsed = textures ? textures->document : legacy;
    parsed.structSize = version5   ? sizeof(materialData)
                        : version4 ? sizeof(full)
                        : version3 ? sizeof(extended)
                                   : sizeof(legacy);
    parsed.version = version5   ? BL_ASSET_DOCUMENT_VERSION_5
                     : version4 ? BL_ASSET_DOCUMENT_VERSION_4
                     : version3 ? BL_ASSET_DOCUMENT_VERSION_3
                                : BL_ASSET_DOCUMENT_VERSION;
    bool invoked = false;
    const char* validationReason = NULL;
    if (status != BL_OK)
    {
        validationReason = "Unable to read GLB URI; check file existence, permissions and size";
    }
    if (status == BL_OK)
    {
        invoked = true;
        L_CALL(runtime, status,
               runtime->assets.parseGltf(runtime->assets.userData, input, source->uri, &parsed));
    }
    if (status == BL_OK)
    {
        status = l_validateAsset(&parsed, &validationReason, textures, accessors,
                                 version5 ? &materialData : NULL);
    }
    if (status == BL_OK)
    {
        status = l_validateAssetHierarchy(runtime, &parsed);
    }
    L_Asset* asset = NULL;
    if (status == BL_OK)
    {
        size_t capacity = parsed.nodeCount + 1;
        for (size_t i = 0; i < parsed.nodeCount; ++i)
        {
            if (parsed.nodes[i].mesh.present)
            {
                size_t added = parsed.meshes[parsed.nodes[i].mesh.value].primitiveCount;
                if (added > SIZE_MAX - capacity)
                {
                    status = BL_INVALID_ARGUMENT;
                    break;
                }
                capacity += added;
            }
        }
        L_Record* record = NULL;
        size_t nodeBytes = 0;
        size_t materialBytes = 0;
        if (status == BL_OK &&
            (!l_size(capacity, sizeof(bl_SceneNode), &nodeBytes) ||
             !l_size(parsed.materialCount + 1, sizeof(bl_Material), &materialBytes) ||
             !l_allocationFits(nodeBytes) || !l_allocationFits(materialBytes)))
        {
            status = BL_INVALID_ARGUMENT;
        }
        if (status == BL_OK)
        {
            status = l_record(runtime, sizeof(L_Asset), L_ASSET, 45, cleanupAsset, &record);
        }
        if (status == BL_OK)
        {
            asset = (L_Asset*)record;
            asset->engine = engine;
            l_pin(&engine->record);
            asset->nodeCapacity = capacity;
            asset->materialCount = parsed.materialCount + 1;
            asset->nodes = (bl_SceneNode*)l_alloc(runtime, nodeBytes);
            asset->materials = (bl_Material*)l_alloc(runtime, materialBytes);
            if (!asset->nodes || !asset->materials)
            {
                status = BL_OUT_OF_MEMORY;
            }
            if (status == BL_OK && textures && parsed.textureCount)
            {
                size_t bytes;
                if (!l_size(parsed.textureCount, 2, &asset->textureCount) ||
                    !l_size(asset->textureCount, sizeof(bl_Texture2D), &bytes) ||
                    !l_allocationFits(bytes))
                {
                    status = BL_INVALID_ARGUMENT;
                }
                else
                {
                    asset->textures = (bl_Texture2D*)l_alloc(runtime, bytes);
                    if (!asset->textures)
                    {
                        status = BL_OUT_OF_MEMORY;
                    }
                }
            }
        }
        if (status == BL_OK)
        {
            bl_TransformNodeOptions rootOptions = {{}, {0, 0, 0, 1}, {-1, 1, 1}};
            status = bl_createTransformNode(runtime, {"__root__", 8}, &rootOptions, &asset->root);
            if (status == BL_OK)
            {
                status = appendAssetNode(asset, asset->root);
            }
        }
        uint32_t scene = parsed.defaultScene.present ? parsed.defaultScene.value : 0;
        if (status == BL_OK && scene < parsed.sceneCount)
        {
            for (size_t i = 0; i < parsed.scenes[scene].rootCount && status == BL_OK; ++i)
            {
                status = buildNode(asset, &parsed, parsed.scenes[scene].roots[i], asset->root,
                                   textures, accessors, version5 ? &materialData : NULL);
            }
        }
    }
    char message[1024] = {};
    uint32_t sourceCode = parsed.diagnostic.sourceErrorCode;
    if (status != BL_OK && validationReason)
    {
        snprintf(message, sizeof(message), "%s", validationReason);
    }
    if (status != BL_OK && l_string(parsed.diagnostic.message))
    {
        size_t length = parsed.diagnostic.message.length < sizeof(message) - 1
                            ? parsed.diagnostic.message.length
                            : sizeof(message) - 1;
        while (length && length < parsed.diagnostic.message.length &&
               ((uint8_t)parsed.diagnostic.message.data[length] & 0xc0) == 0x80)
        {
            --length;
        }
        memcpy(message, parsed.diagnostic.message.data, length);
    }
    if (invoked)
    {
        l_enterHost(runtime);
        runtime->assets.releaseGltf(runtime->assets.userData, &parsed);
        l_leaveHost(runtime);
    }
    if (read)
    {
        l_enterHost(runtime);
        runtime->io.releaseBuffer(runtime->io.userData, &buffer);
        l_leaveHost(runtime);
    }
    if (status != BL_OK)
    {
        if (asset)
        {
            bl_Status retired = l_retire(engine);
            if (retired == BL_OK)
            {
                cleanupAsset(runtime, &asset->record);
            }
        }
        return l_error(runtime, status, __func__, message[0] ? message : "Runtime GLB load failed",
                       sourceCode);
    }
    *out = {runtime, asset->record.id};
    return BL_OK;
}
