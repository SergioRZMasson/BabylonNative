#include "AssetInternal.h"

static bool inspectionOutput(const void* destination, size_t capacity, size_t elementSize,
                             size_t alignment, const size_t* count)
{
    size_t bytes;
    return count && l_size(capacity, elementSize, &bytes) &&
           l_typedSpan(destination, capacity, alignment);
}

static size_t sceneInspectionCount(L_Scene* scene, unsigned kind)
{
    size_t count = 0;
    for (size_t i = 0; i < scene->memberCount; ++i)
    {
        L_Record* record = &scene->members[i]->record;
        if (!record->disposed && record->kind == kind)
        {
            ++count;
        }
    }
    return count;
}

bl_Status bl_getSceneMeshes(bl_SceneContext handle, bl_Mesh* destination, size_t capacity,
                            size_t* count)
{
    L_GET(handle, L_SCENE, L_Scene, scene);
    if (!inspectionOutput(destination, capacity, sizeof(bl_Mesh), alignof(bl_Mesh), count))
    {
        return BL_INVALID_ARGUMENT;
    }
    size_t required = sceneInspectionCount(scene, L_MESH);
    if (destination && capacity < required)
    {
        return BL_INVALID_ARGUMENT;
    }
    size_t index = 0;
    if (destination)
    {
        for (size_t i = 0; i < scene->memberCount; ++i)
        {
            L_Record* record = &scene->members[i]->record;
            if (!record->disposed && record->kind == L_MESH)
            {
                destination[index++] = {handle._runtime, record->id};
            }
        }
    }
    *count = required;
    return BL_OK;
}

bl_Status bl_getSceneLights(bl_SceneContext handle, bl_Light* destination, size_t capacity,
                            size_t* count)
{
    L_GET(handle, L_SCENE, L_Scene, scene);
    if (!inspectionOutput(destination, capacity, sizeof(bl_Light), alignof(bl_Light), count))
    {
        return BL_INVALID_ARGUMENT;
    }
    size_t required = sceneInspectionCount(scene, L_LIGHT);
    if (destination && capacity < required)
    {
        return BL_INVALID_ARGUMENT;
    }
    size_t index = 0;
    if (destination)
    {
        for (size_t i = 0; i < scene->memberCount; ++i)
        {
            L_Record* record = &scene->members[i]->record;
            if (!record->disposed && record->kind == L_LIGHT)
            {
                destination[index++] = {handle._runtime, record->id};
            }
        }
    }
    *count = required;
    return BL_OK;
}

struct L_InspectionVisit
{
    L_Node* node;
    size_t nextChild;
};

static bl_Status containerInspection(bl_Runtime* runtime, L_Asset* asset, bl_Mesh* meshes,
                                     L_InspectionVisit* stack, uint8_t* visited, size_t* count)
{
    if (asset->root._runtime != runtime)
    {
        return BL_WRONG_RUNTIME;
    }
    L_Node* root = NULL;
    L_TRY(l_node(asset->root, &root));
    stack[0] = {root, 0};
    visited[(uint32_t)root->record.id - 1] = 1;
    size_t depth = 1;
    *count = 0;
    if (root->record.kind == L_MESH)
    {
        meshes[(*count)++] = {runtime, root->record.id};
    }
    while (depth)
    {
        L_InspectionVisit* frame = &stack[depth - 1];
        if (frame->nextChild == frame->node->childCount)
        {
            --depth;
            continue;
        }
        bl_SceneNode child = frame->node->children[frame->nextChild++];
        if (child._runtime != runtime)
        {
            return BL_WRONG_RUNTIME;
        }
        L_Node* node = NULL;
        L_TRY(l_node(child, &node, true));
        if (!node || node->record.disposed || visited[(uint32_t)node->record.id - 1])
        {
            continue;
        }
        visited[(uint32_t)node->record.id - 1] = 1;
        if (node->record.kind == L_MESH)
        {
            meshes[(*count)++] = {runtime, node->record.id};
        }
        stack[depth++] = {node, 0};
    }
    return BL_OK;
}

bl_Status bl_getContainerMeshes(bl_AssetContainer handle, bl_Mesh* destination, size_t capacity,
                                size_t* count)
{
    L_GET(handle, L_ASSET, L_Asset, asset);
    if (!inspectionOutput(destination, capacity, sizeof(bl_Mesh), alignof(bl_Mesh), count))
    {
        return BL_INVALID_ARGUMENT;
    }
    bl_Runtime* runtime = handle._runtime;
    size_t meshBytes;
    size_t stackBytes;
    if (!l_size(runtime->count, sizeof(bl_Mesh), &meshBytes) ||
        !l_size(runtime->count, sizeof(L_InspectionVisit), &stackBytes))
    {
        return BL_OUT_OF_MEMORY;
    }
    bl_Mesh* meshes = (bl_Mesh*)l_alloc(runtime, meshBytes);
    L_InspectionVisit* stack = (L_InspectionVisit*)l_alloc(runtime, stackBytes);
    uint8_t* visited = (uint8_t*)l_alloc(runtime, runtime->count);
    bl_Status status = BL_OUT_OF_MEMORY;
    size_t required = 0;
    if (meshes && stack && visited)
    {
        memset(visited, 0, runtime->count);
        status = containerInspection(runtime, asset, meshes, stack, visited, &required);
        if (status == BL_OK && destination && capacity < required)
        {
            status = BL_INVALID_ARGUMENT;
        }
        if (status == BL_OK)
        {
            if (destination && required)
            {
                memcpy(destination, meshes, required * sizeof(bl_Mesh));
            }
            *count = required;
        }
    }
    l_free(runtime, meshes);
    l_free(runtime, stack);
    l_free(runtime, visited);
    return status;
}

bl_Status bl_getMeshBounds(bl_Mesh handle, bl_Vec3* minimum, bl_Vec3* maximum)
{
    L_GET(handle, L_MESH, L_Mesh, mesh);
    if (!minimum || !maximum)
    {
        return BL_INVALID_ARGUMENT;
    }
    *minimum = mesh->geometry.minimum;
    *maximum = mesh->geometry.maximum;
    return BL_OK;
}

bl_Status bl_getMaterialName(bl_Material handle, bl_String* name)
{
    L_GET(handle, 0, L_Material, material);
    if (material->record.kind != L_MATERIAL && material->record.kind != L_STANDARD &&
        material->record.kind != L_PBR)
    {
        return BL_INVALID_HANDLE;
    }
    if (!name)
    {
        return BL_INVALID_ARGUMENT;
    }
    *name = material->name;
    return BL_OK;
}
