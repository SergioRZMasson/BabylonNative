/*
 * Investigation adaptation of parseGltfCpu/decodeImageCpu/uploadGltfGpu from:
 * bghgary/BabylonLiteNativePort, ca5b8fdafdbc034982982210d1daa20dbe210218,
 * Source/GltfLoader.cpp (Gary Hsu). Used with explicit investigation permission.
 * No blanket license assertion is made for that prototype repository.
 *
 * Retains the cgltf -> checked CPU data -> native GPU-resource flow, not its
 * flattened scene, coordinate normalization, platform codec or detached worker.
 */
#include "GltfInternal.h"
#include "AssetMotionInternal.h"

static void* allocate(void* user, cgltf_size bytes)
{
    L_Gltf* gltf = (L_Gltf*)user;
    L_GltfAllocation* allocation = NULL;
    if (bytes <= SIZE_MAX - sizeof(L_GltfAllocation))
    {
        allocation = (L_GltfAllocation*)l_alloc(gltf->runtime,
                                                sizeof(L_GltfAllocation) + (bytes ? bytes : 1));
    }
    if (!allocation)
    {
        if (gltf->parseGuard)
        {
            longjmp(gltf->parseJump, 1);
        }
        return NULL;
    }
    allocation->next = gltf->allocations;
    if (gltf->allocations)
    {
        gltf->allocations->previous = allocation;
    }
    gltf->allocations = allocation;
    return allocation + 1;
}

static void release(void* user, void* memory)
{
    if (!memory)
    {
        return;
    }
    L_Gltf* gltf = (L_Gltf*)user;
    L_GltfAllocation* allocation = (L_GltfAllocation*)memory - 1;
    if (allocation->previous)
    {
        allocation->previous->next = allocation->next;
    }
    else
    {
        gltf->allocations = allocation->next;
    }
    if (allocation->next)
    {
        allocation->next->previous = allocation->previous;
    }
    l_free(gltf->runtime, allocation);
}

void l_gltfFreeAllocations(L_Gltf* gltf)
{
    while (gltf->allocations)
    {
        release(gltf, gltf->allocations + 1);
    }
}

bl_Status l_gltfParse(L_Gltf* gltf, const uint8_t* input, size_t bytes)
{
    gltf->parseGuard = true;
    cgltf_result result = l_cgltfParse(&gltf->options, input, bytes, &gltf->data, &gltf->parseJump);
    gltf->parseGuard = false;
    if (result == cgltf_result_out_of_memory)
    {
        gltf->data = NULL;
        l_gltfFreeAllocations(gltf);
    }
    return l_gltfResult(gltf, result);
}

bl_Status l_gltfResult(L_Gltf* gltf, cgltf_result result)
{
    if (result == cgltf_result_success)
    {
        return BL_OK;
    }
    if (gltf->ioStatus != BL_OK)
    {
        return gltf->ioStatus;
    }
    return result == cgltf_result_out_of_memory ? BL_OUT_OF_MEMORY
           : result == cgltf_result_io_error || result == cgltf_result_file_not_found
               ? BL_HOST_ERROR
               : BL_INVALID_ARGUMENT;
}

static bl_Status readFile(L_Gltf* gltf, const char* path, uint8_t** out, size_t* bytes)
{
    bl_Runtime* runtime = gltf->runtime;
    if (!runtime->hasIO || !runtime->io.readFile || !runtime->io.releaseBuffer)
    {
        return BL_UNSUPPORTED;
    }
    bl_HostBuffer buffer = {};
    bl_Status status;
    L_CALL(runtime, status,
           runtime->io.readFile(runtime->io.userData, {path, strlen(path)}, &buffer));
    uint8_t* copy = NULL;
    if (status == BL_OK &&
        (!buffer.data || !buffer.byteCount || !l_span(buffer.data, buffer.byteCount) ||
         buffer.byteCount > SIZE_MAX - 1 || !l_allocationFits(buffer.byteCount + 1)))
    {
        status = BL_INVALID_ARGUMENT;
    }
    if (status == BL_OK)
    {
        copy = (uint8_t*)l_alloc(runtime, buffer.byteCount + 1);
        if (!copy)
        {
            status = BL_OUT_OF_MEMORY;
        }
        else
        {
            memcpy(copy, buffer.data, buffer.byteCount);
            *bytes = buffer.byteCount;
            *out = copy;
        }
    }
    l_enterHost(runtime);
    runtime->io.releaseBuffer(runtime->io.userData, &buffer);
    l_leaveHost(runtime);
    return status;
}

static cgltf_result fileRead(const cgltf_memory_options*, const cgltf_file_options* options,
                             const char* path, cgltf_size* bytes, void** out)
{
    L_Gltf* gltf = (L_Gltf*)options->user_data;
    size_t actual = 0;
    uint8_t* copy = NULL;
    bl_Status status = readFile(gltf, path, &copy, &actual);
    if (status == BL_OK && actual < *bytes)
    {
        status = BL_INVALID_ARGUMENT;
    }
    if (status != BL_OK)
    {
        l_free(gltf->runtime, copy);
        gltf->ioStatus = status;
        return status == BL_OUT_OF_MEMORY ? cgltf_result_out_of_memory : cgltf_result_io_error;
    }
    *bytes = actual;
    *out = copy;
    return cgltf_result_success;
}

static void fileRelease(const cgltf_memory_options* memory, const cgltf_file_options*, void* data,
                        cgltf_size)
{
    l_free(((L_Gltf*)memory->user_data)->runtime, data);
}

bl_Status l_gltfReadUri(L_Gltf* gltf, const char* uri, uint8_t** out, size_t* bytes)
{
    if (strstr(uri, "://"))
    {
        gltf->reason = "Remote resource URIs are unsupported by the native file integration";
        return BL_UNSUPPORTED;
    }
    size_t prefix = 0;
    for (size_t i = 0; gltf->path[i]; ++i)
    {
        if (gltf->path[i] == '/' || gltf->path[i] == '\\')
        {
            prefix = i + 1;
        }
    }
    size_t length = strlen(uri);
    if (length > SIZE_MAX - prefix - 1)
    {
        return BL_INVALID_ARGUMENT;
    }
    char* path = (char*)l_alloc(gltf->runtime, prefix + length + 1);
    if (!path)
    {
        return BL_OUT_OF_MEMORY;
    }
    memcpy(path, gltf->path, prefix);
    memcpy(path + prefix, uri, length + 1);
    cgltf_decode_uri(path + prefix);
    bl_Status status = readFile(gltf, path, out, bytes);
    l_free(gltf->runtime, path);
    return status;
}

static bool supportedExtension(const char* name)
{
    return !strcmp(name, "EXT_mesh_gpu_instancing") ||
           !strcmp(name, "KHR_materials_pbrSpecularGlossiness");
}

static bl_Status validateFeatures(L_Gltf* gltf)
{
    const cgltf_data* data = gltf->data;
    if (!data->asset.version || strcmp(data->asset.version, "2.0") ||
        (data->asset.min_version && strcmp(data->asset.min_version, "2.0")))
    {
        gltf->reason = "Only glTF 2.0 is supported";
        return BL_UNSUPPORTED;
    }
    for (size_t i = 0; i < data->cameras_count; ++i)
    {
        const cgltf_camera* camera = data->cameras + i;
        if (camera->type == cgltf_camera_type_perspective)
        {
            const cgltf_camera_perspective* projection = &camera->data.perspective;
            if (!isfinite(projection->znear) || projection->znear <= 0 ||
                !isfinite(projection->yfov) || projection->yfov <= 0 ||
                (projection->has_zfar &&
                 (!isfinite(projection->zfar) || projection->zfar <= projection->znear)) ||
                (projection->has_aspect_ratio &&
                 (!isfinite(projection->aspect_ratio) || projection->aspect_ratio <= 0)))
            {
                gltf->reason = "Invalid ignored perspective camera definition";
                return BL_INVALID_ARGUMENT;
            }
        }
        else if (camera->type == cgltf_camera_type_orthographic)
        {
            const cgltf_camera_orthographic* projection = &camera->data.orthographic;
            if (!isfinite(projection->xmag) || !isfinite(projection->ymag) ||
                !isfinite(projection->znear) || !isfinite(projection->zfar) || !projection->xmag ||
                !projection->ymag || projection->znear < 0 || projection->zfar <= projection->znear)
            {
                gltf->reason = "Invalid ignored orthographic camera definition";
                return BL_INVALID_ARGUMENT;
            }
        }
        else
        {
            gltf->reason = "Invalid ignored camera type";
            return BL_INVALID_ARGUMENT;
        }
    }
    for (size_t i = 0; i < data->extensions_required_count; ++i)
    {
        if (!supportedExtension(data->extensions_required[i]))
        {
            gltf->reason = "Unsupported extensionsRequired entry";
            return BL_UNSUPPORTED;
        }
    }
    for (size_t i = 0; i < data->extensions_used_count; ++i)
    {
        if (!supportedExtension(data->extensions_used[i]))
        {
            gltf->reason = "Unsupported extensionsUsed entry";
            return BL_UNSUPPORTED;
        }
    }
    if (data->data_extensions_count || data->asset.extensions_count || data->lights_count)
    {
        gltf->reason = "Unsupported document or asset extension";
        return BL_UNSUPPORTED;
    }
    for (size_t i = 0; i < data->buffers_count; ++i)
    {
        const cgltf_buffer* buffer = data->buffers + i;
        if (buffer->extensions_count)
        {
            return BL_UNSUPPORTED;
        }
        if (buffer->uri && !strncmp(buffer->uri, "data:", 5))
        {
            const char* comma = strchr(buffer->uri, ',');
            if (!comma || comma - buffer->uri < 7 || strncmp(comma - 7, ";base64", 7) ||
                buffer->size > SIZE_MAX - 2 || (buffer->size + 2) / 3 > SIZE_MAX / 4 ||
                strlen(comma + 1) < (buffer->size + 2) / 3 * 4)
            {
                return BL_INVALID_ARGUMENT;
            }
        }
    }
    return BL_OK;
}

static bl_Status validateDocument(L_Gltf* gltf)
{
    size_t bytes;
    if (!l_size(gltf->data->accessors_count ? gltf->data->accessors_count : 1,
                sizeof(cgltf_buffer_view*), &bytes))
    {
        return BL_INVALID_ARGUMENT;
    }
    cgltf_buffer_view** views = (cgltf_buffer_view**)l_alloc(gltf->runtime, bytes);
    if (!views)
    {
        return BL_OUT_OF_MEMORY;
    }
    // The library checks base indices before sparse replacement. Bounds and
    // sparse storage are already checked; final indices are checked at gathering.
    for (size_t i = 0; i < gltf->data->accessors_count; ++i)
    {
        cgltf_accessor* accessor = gltf->data->accessors + i;
        views[i] = accessor->buffer_view;
        if (accessor->is_sparse)
        {
            accessor->buffer_view = NULL;
        }
    }
    cgltf_result result = cgltf_validate(gltf->data);
    for (size_t i = 0; i < gltf->data->accessors_count; ++i)
    {
        gltf->data->accessors[i].buffer_view = views[i];
    }
    l_free(gltf->runtime, views);
    return l_gltfResult(gltf, result);
}

static bl_Status topology(L_Gltf* gltf)
{
    const cgltf_data* data = gltf->data;
    uint8_t* parents = (uint8_t*)l_alloc(gltf->runtime, data->nodes_count ? data->nodes_count : 1);
    if (!parents)
    {
        return BL_OUT_OF_MEMORY;
    }
    bl_Status status = BL_OK;
    for (size_t i = 0; i < data->nodes_count && status == BL_OK; ++i)
    {
        const cgltf_node* node = data->nodes + i;
        if (node->weights_count || node->light || node->extensions_count)
        {
            status = BL_UNSUPPORTED;
        }
        for (size_t j = 0; j < node->children_count && status == BL_OK; ++j)
        {
            size_t child = (size_t)(node->children[j] - data->nodes);
            if (parents[child]++ || node->children[j]->parent != node)
            {
                status = BL_INVALID_ARGUMENT;
            }
        }
    }
    l_free(gltf->runtime, parents);
    return status;
}

static bl_Status createNode(L_Gltf* gltf, L_Asset* asset, const cgltf_node* source,
                            bl_TransformNode parent, bl_TransformNode* out)
{
    if (source->has_matrix &&
        (source->has_translation || source->has_rotation || source->has_scale))
    {
        return BL_INVALID_ARGUMENT;
    }
    bl_TransformNodeOptions options = {
        {source->translation[0], source->translation[1], source->translation[2]},
        {source->rotation[0], source->rotation[1], source->rotation[2], source->rotation[3]},
        {source->scale[0], source->scale[1], source->scale[2]}};
    if (source->has_matrix)
    {
        for (size_t i = 0; i < 16; ++i)
        {
            if (!isfinite(source->matrix[i]))
            {
                return BL_INVALID_ARGUMENT;
            }
        }
        if (source->matrix[3] || source->matrix[7] || source->matrix[11] || source->matrix[15] != 1)
        {
            return BL_UNSUPPORTED;
        }
        options = {{}, {0, 0, 0, 1}, {1, 1, 1}};
    }
    else if (asset->motion)
    {
        options.rotationQuaternion =
            asset->motion->rest[source - gltf->data->nodes].rotationQuaternion;
    }
    char fallback[64];
    snprintf(fallback, sizeof(fallback), "node_%zu", (size_t)(source - gltf->data->nodes));
    const char* name = source->name ? source->name : fallback;
    bl_TransformNode node = {};
    L_TRY(bl_createTransformNode(gltf->runtime, {name, strlen(name)}, &options, &node));
    L_TRY(l_appendAssetNode(asset, node));
    if (asset->motion)
    {
        asset->motion->nodes[source - gltf->data->nodes] = (L_Node*)l_peek(gltf->runtime, node._id);
    }
    if (source->has_matrix)
    {
        L_Node* record = (L_Node*)l_peek(gltf->runtime, node._id);
        record->localMatrixLocked = true;
        for (size_t i = 0; i < 16; ++i)
        {
            record->lockedLocalMatrix.values[i] = source->matrix[i];
        }
    }
    L_TRY(bl_appendNodeChild(parent, node));
    L_TRY(bl_setNodeParent(node, parent));
    *out = node;
    return BL_OK;
}

struct L_GltfFrame
{
    const cgltf_node* source;
    bl_TransformNode parent;
};

static bl_Status build(L_Gltf* gltf, L_Engine* engine, L_Asset** out)
{
    const cgltf_data* data = gltf->data;
    size_t capacity = data->nodes_count + 1;
    size_t textureCount;
    size_t stackBytes;
    if (!capacity || !l_size(data->textures_count, 2, &textureCount) ||
        !l_size(data->nodes_count ? data->nodes_count : 1, sizeof(L_GltfFrame), &stackBytes))
    {
        return BL_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < data->nodes_count; ++i)
    {
        size_t added = data->nodes[i].mesh ? data->nodes[i].mesh->primitives_count : 0;
        if (added > SIZE_MAX - capacity)
        {
            return BL_INVALID_ARGUMENT;
        }
        capacity += added;
    }
    L_TRY(l_createAsset(engine, capacity, data->materials_count + 1, textureCount, out));
    L_Asset* asset = *out;
    L_TRY(l_gltfCreateMotion(gltf, asset));
    const cgltf_scene* scene = data->scene ? data->scene : data->scenes_count ? data->scenes : NULL;
    if (!scene)
    {
        return l_finishAssetMotion(asset);
    }
    if (scene->extensions_count || scene->nodes_count > data->nodes_count)
    {
        return BL_INVALID_ARGUMENT;
    }
    L_GltfFrame* stack = (L_GltfFrame*)l_alloc(gltf->runtime, stackBytes);
    uint8_t* seen = (uint8_t*)l_alloc(gltf->runtime, data->nodes_count ? data->nodes_count : 1);
    if (!stack || !seen)
    {
        l_free(gltf->runtime, stack);
        l_free(gltf->runtime, seen);
        return BL_OUT_OF_MEMORY;
    }
    size_t count = 0;
    for (size_t i = scene->nodes_count; i > 0; --i)
    {
        stack[count++] = {scene->nodes[i - 1], asset->root};
    }
    bl_Status status = BL_OK;
    while (count && status == BL_OK)
    {
        L_GltfFrame frame = stack[--count];
        const cgltf_node* node = frame.source;
        size_t index = (size_t)(node - data->nodes);
        if (seen[index] || node->children_count > data->nodes_count - count)
        {
            status = BL_INVALID_ARGUMENT;
            break;
        }
        seen[index] = 1;
        bl_TransformNode handle = {};
        status = createNode(gltf, asset, node, frame.parent, &handle);
        if (node->mesh)
        {
            for (size_t i = 0; i < node->mesh->primitives_count && status == BL_OK; ++i)
            {
                status = l_gltfMesh(gltf, asset, node, node->mesh->primitives + i, handle);
            }
        }
        for (size_t i = node->children_count; i > 0 && status == BL_OK; --i)
        {
            stack[count++] = {node->children[i - 1], handle};
        }
    }
    l_free(gltf->runtime, seen);
    l_free(gltf->runtime, stack);
    if (status == BL_OK)
    {
        status = l_finishAssetMotion(asset);
    }
    return status;
}

bl_Status l_loadNativeGltf(L_Engine* engine, const bl_AssetSource* source, bl_AssetContainer* out)
{
    bl_Runtime* runtime = engine->runtime;
    L_Gltf gltf = {};
    gltf.runtime = runtime;
    gltf.options.memory = {allocate, release, &gltf};
    gltf.options.file = {fileRead, fileRelease, &gltf};
    bl_String path = l_copyString(runtime, source->uri);
    if (!path.data)
    {
        return BL_OUT_OF_MEMORY;
    }
    gltf.path = path.data;
    uint8_t* owned = NULL;
    size_t bytes = source->bytes.count;
    bl_Status status = BL_OK;
    if (source->kind == BL_ASSET_SOURCE_URI)
    {
        status = readFile(&gltf, gltf.path, &owned, &bytes);
    }
    else if (bytes == SIZE_MAX)
    {
        status = BL_INVALID_ARGUMENT;
    }
    else
    {
        owned = (uint8_t*)l_alloc(runtime, bytes + 1);
        if (!owned)
        {
            status = BL_OUT_OF_MEMORY;
        }
        else
        {
            memcpy(owned, source->bytes.data, bytes);
        }
    }
    if (status == BL_OK)
    {
        status = l_gltfParse(&gltf, owned, bytes);
    }
    if (status == BL_OK)
    {
        cgltf_result presence = l_cgltfValidatePresence(&gltf.options, gltf.data);
        status = presence == cgltf_result_unknown_format ? BL_UNSUPPORTED
                                                         : l_gltfResult(&gltf, presence);
        if (presence == cgltf_result_unknown_format)
        {
            gltf.reason = "Unsupported animation interpolation or target extension";
        }
    }
    if (status == BL_OK)
    {
        status = validateFeatures(&gltf);
    }
    if (status == BL_OK)
    {
        status = l_gltfResult(&gltf, cgltf_load_buffers(&gltf.options, gltf.data, gltf.path));
    }
    if (status == BL_OK)
    {
        status = l_gltfValidateAccessors(&gltf);
    }
    if (status == BL_OK)
    {
        status = l_gltfValidateMotion(&gltf);
        if (status != BL_OK)
        {
            gltf.reason = status == BL_UNSUPPORTED
                              ? "Unsupported skin/animation domain or extension"
                          : status == BL_INVALID_ARGUMENT
                              ? "Invalid skin/animation accessor, influence or hierarchy"
                              : "Unable to construct skin/animation storage";
        }
    }
    if (status == BL_OK)
    {
        status = validateDocument(&gltf);
    }
    if (status == BL_OK)
    {
        status = topology(&gltf);
    }
    L_Asset* asset = NULL;
    if (status == BL_OK)
    {
        status = build(&gltf, engine, &asset);
    }
    cgltf_free(gltf.data);
    l_gltfFreeAllocations(&gltf);
    l_free(runtime, owned);
    l_free(runtime, (void*)path.data);
    if (status != BL_OK)
    {
        l_abortAsset(asset);
        return l_error(runtime, status, "bl_loadGltf",
                       gltf.reason ? gltf.reason : "Native cgltf import failed", 0);
    }
    *out = {runtime, asset->record.id};
    return BL_OK;
}
