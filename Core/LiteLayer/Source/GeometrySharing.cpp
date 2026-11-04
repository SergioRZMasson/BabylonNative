/*
 * Explicit claims corresponding to original scene/transform-node.ts and
 * resource/ref-count.ts (Lite 1.32.0, 2e064d88, Apache-2.0). CPU array retention
 * models the original GC references separately from shared MeshGPU identity.
 */
#include "LiteInternal.h"

struct L_CpuGeometryOwner
{
    size_t owners;
    float* streams[6];
    uint32_t* indices;
};

struct L_GpuGeometryOwner
{
    size_t owners;
    bgfx::DynamicVertexBufferHandle vertices;
    bgfx::DynamicIndexBufferHandle indices;
    uint8_t* vertexMirror;
    uint32_t* indexMirror;
    bool dedicated;
};

struct L_ThinOwner
{
    size_t owners;
};

bool l_sharedGeometryGpu(const L_Geometry* geometry)
{
    return geometry->gpuOwner && geometry->gpuOwner->owners > 1;
}

void l_syncGeometryGpu(L_Geometry* g)
{
    if (g->gpuOwner)
    {
        const L_GpuGeometryOwner* owner = g->gpuOwner;
        g->vertexBuffer = owner->vertices;
        g->indexBuffer = owner->indices;
        g->gpuVertices = owner->vertexMirror;
        g->gpuIndices = owner->indexMirror;
        g->dedicatedIndices = owner->dedicated;
    }
}

void l_publishGeometryGpu(L_Geometry* g)
{
    if (g->gpuOwner)
    {
        L_GpuGeometryOwner* owner = g->gpuOwner;
        owner->vertices = g->vertexBuffer;
        owner->indices = g->indexBuffer;
        owner->vertexMirror = g->gpuVertices;
        owner->indexMirror = g->gpuIndices;
        owner->dedicated = g->dedicatedIndices;
    }
}

void l_replaceSharedGpuMirrors(bl_Runtime* r, L_Geometry* old, L_Geometry* next)
{
    if (old->gpuOwner)
    {
        L_GpuGeometryOwner* owner = old->gpuOwner;
        l_free(r, owner->vertexMirror);
        l_free(r, owner->indexMirror);
        owner->vertexMirror = next->gpuVertices;
        owner->indexMirror = next->gpuIndices;
        ++owner->owners;
        next->gpuOwner = owner;
        l_syncGeometryGpu(old);
        l_syncGeometryGpu(next);
    }
}

static void collapseCpu(bl_Runtime* r, L_Geometry* released, L_CpuGeometryOwner* owner)
{
    for (size_t i = 0; i < r->count; ++i)
    {
        L_Record* record = r->records[i];
        if (record && !record->disposed && record->kind == L_MESH)
        {
            L_Geometry* g = &((L_Mesh*)record)->geometry;
            if (g != released && g->cpuOwner == owner)
            {
                g->cpuOwner = NULL;
                l_free(r, owner);
                return;
            }
        }
    }
}

static void collapseGpu(bl_Runtime* r, L_Geometry* released, L_GpuGeometryOwner* owner)
{
    for (size_t i = 0; i < r->count; ++i)
    {
        L_Record* record = r->records[i];
        if (record && !record->disposed && record->kind == L_MESH)
        {
            L_Geometry* g = &((L_Mesh*)record)->geometry;
            if (g != released && g->gpuOwner == owner)
            {
                l_syncGeometryGpu(g);
                g->gpuOwner = NULL;
                l_free(r, owner);
                return;
            }
        }
    }
}

void l_releaseGeometryStorage(bl_Runtime* r, L_Geometry* g)
{
    l_syncGeometryGpu(g);
    bool cpu = !g->cpuOwner || --g->cpuOwner->owners == 0;
    bool gpu = !g->gpuOwner || --g->gpuOwner->owners == 0;
    if (cpu)
    {
        for (unsigned i = 0; i < 6; ++i)
        {
            l_free(r, g->streams[i]);
        }
        l_free(r, g->indices);
        l_free(r, g->cpuOwner);
    }
    else if (g->cpuOwner->owners == 1)
    {
        collapseCpu(r, g, g->cpuOwner);
    }
    if (gpu)
    {
        if (bgfx::isValid(g->vertexBuffer))
        {
            bgfx::destroy(g->vertexBuffer);
        }
        if (bgfx::isValid(g->indexBuffer))
        {
            bgfx::destroy(g->indexBuffer);
        }
        l_free(r, g->gpuVertices);
        l_free(r, g->gpuIndices);
        l_free(r, g->gpuOwner);
    }
    else if (g->gpuOwner->owners == 1)
    {
        collapseGpu(r, g, g->gpuOwner);
    }
}

static bl_Status shareGeometry(bl_Runtime* r, L_Geometry* source, L_Geometry* out)
{
    l_syncGeometryGpu(source);
    L_CpuGeometryOwner* cpu = source->cpuOwner;
    L_GpuGeometryOwner* gpu = source->gpuOwner;
    if ((cpu && cpu->owners == SIZE_MAX) || (gpu && gpu->owners == SIZE_MAX))
    {
        return BL_OUT_OF_MEMORY;
    }
    if (!cpu)
    {
        cpu = (L_CpuGeometryOwner*)l_alloc(r, sizeof(*cpu));
        if (!cpu)
        {
            return BL_OUT_OF_MEMORY;
        }
        cpu->owners = 1;
        memcpy(cpu->streams, source->streams, sizeof(cpu->streams));
        cpu->indices = source->indices;
    }
    if (!gpu)
    {
        gpu = (L_GpuGeometryOwner*)l_alloc(r, sizeof(*gpu));
        if (!gpu)
        {
            if (!source->cpuOwner)
            {
                l_free(r, cpu);
            }
            return BL_OUT_OF_MEMORY;
        }
        *gpu = {1,
                source->vertexBuffer,
                source->indexBuffer,
                source->gpuVertices,
                source->gpuIndices,
                source->dedicatedIndices};
    }
    source->cpuOwner = cpu;
    source->gpuOwner = gpu;
    ++cpu->owners;
    ++gpu->owners;
    *out = *source;
    return BL_OK;
}

static void copyThin(L_Mesh* to, const L_Mesh* from)
{
    to->instanceMatrices = from->instanceMatrices;
    to->instanceMatrices64 = from->instanceMatrices64;
    to->instanceColors = from->instanceColors;
    to->instanceColorCount = from->instanceColorCount;
    to->instanceColorVersion = from->instanceColorVersion;
    to->instanceDirtyMin = from->instanceDirtyMin;
    to->instanceDirtyMax = from->instanceDirtyMax;
    to->instanceColorDirtyMin = from->instanceColorDirtyMin;
    to->instanceColorDirtyMax = from->instanceColorDirtyMax;
    to->instanceCulling = from->instanceCulling;
    to->instanceGpuStride = from->instanceGpuStride;
    to->instanceGpuCapacity = from->instanceGpuCapacity;
    to->instanceUpload = from->instanceUpload;
    to->instanceUploadFloats = from->instanceUploadFloats;
    to->instanceFloatCount = from->instanceFloatCount;
    to->instanceCount = from->instanceCount;
    to->instanceCapacity = from->instanceCapacity;
    to->instanceVersion = from->instanceVersion;
    to->instanceGpuVersion = from->instanceGpuVersion;
    to->hasInstances = from->hasInstances;
    to->instanceBufferIndex = from->instanceBufferIndex;
}

void l_publishThinState(bl_Runtime* r, L_Mesh* mesh)
{
    if (!mesh->thinOwner)
    {
        return;
    }
    for (size_t i = 0; i < r->count; ++i)
    {
        L_Record* record = r->records[i];
        if (record && !record->disposed && record->kind == L_MESH)
        {
            L_Mesh* other = (L_Mesh*)record;
            if (other != mesh && other->thinOwner == mesh->thinOwner)
            {
                copyThin(other, mesh);
            }
        }
    }
}

bool l_releaseThinOwner(bl_Runtime* r, L_Mesh* mesh)
{
    L_ThinOwner* owner = mesh->thinOwner;
    if (!owner)
    {
        return true;
    }
    if (--owner->owners == 0)
    {
        l_free(r, owner);
        mesh->thinOwner = NULL;
        return true;
    }
    if (owner->owners == 1)
    {
        for (size_t i = 0; i < r->count; ++i)
        {
            L_Record* record = r->records[i];
            if (record && !record->disposed && record->kind == L_MESH)
            {
                L_Mesh* other = (L_Mesh*)record;
                if (other != mesh && other->thinOwner == owner)
                {
                    other->thinOwner = NULL;
                    l_free(r, owner);
                    break;
                }
            }
        }
    }
    mesh->thinOwner = NULL;
    return false;
}

bl_Status l_cloneMeshRecord(bl_Runtime* r, L_Record* record, bl_String name, L_Record** out)
{
    L_Mesh* source = (L_Mesh*)record;
    if (source->skin)
    {
        return L_FAIL(r, BL_UNSUPPORTED,
                      "Cloning a bound CPU skin requires shared deformation ownership");
    }
    bl_String copied = l_copyString(r, name);
    if (!copied.data)
    {
        return BL_OUT_OF_MEMORY;
    }
    uint32_t* lines = NULL;
    if (source->lineCount)
    {
        size_t bytes;
        if (!l_size(source->lineCount, sizeof(uint32_t), &bytes))
        {
            l_free(r, (void*)copied.data);
            return BL_INVALID_ARGUMENT;
        }
        lines = (uint32_t*)l_alloc(r, bytes);
        if (!lines)
        {
            l_free(r, (void*)copied.data);
            return BL_OUT_OF_MEMORY;
        }
        memcpy(lines, source->linePointCounts, bytes);
    }
    L_Geometry geometry;
    bl_Status status = shareGeometry(r, &source->geometry, &geometry);
    if (status != BL_OK)
    {
        l_free(r, (void*)copied.data);
        l_free(r, lines);
        return status;
    }
    L_ThinOwner* thin = source->thinOwner;
    bool createdThin = source->hasInstances && !thin;
    if (createdThin)
    {
        thin = (L_ThinOwner*)l_alloc(r, sizeof(*thin));
        if (thin)
        {
            thin->owners = 1;
        }
    }
    if ((source->hasInstances && !thin) || (thin && thin->owners == SIZE_MAX))
    {
        l_geometryCleanup(r, &geometry);
        l_free(r, (void*)copied.data);
        l_free(r, lines);
        if (createdThin)
        {
            l_free(r, thin);
        }
        return BL_OUT_OF_MEMORY;
    }
    L_Record* cloned = NULL;
    status = l_record(r, sizeof(L_Mesh), L_MESH, 30, l_meshCleanup, &cloned);
    if (status != BL_OK)
    {
        l_geometryCleanup(r, &geometry);
        l_free(r, (void*)copied.data);
        l_free(r, lines);
        if (createdThin)
        {
            l_free(r, thin);
        }
        return status;
    }
    L_Mesh* mesh = (L_Mesh*)cloned;
    l_initNode(&mesh->node);
    mesh->node.record.clone = l_cloneMeshRecord;
    mesh->node.name = copied;
    mesh->node.visible = source->node.visible;
    mesh->engine = source->engine;
    mesh->geometry = geometry;
    mesh->properties = source->properties;
    mesh->material = source->material;
    mesh->assetFlatNormal = source->assetFlatNormal;
    mesh->topology = source->topology;
    mesh->linePointCounts = lines;
    mesh->lineCount = source->lineCount;
    mesh->instanceWorldBounds = source->instanceWorldBounds;
    copyThin(mesh, source);
    if (thin)
    {
        ++thin->owners;
        source->thinOwner = thin;
        mesh->thinOwner = thin;
    }
    l_pin(&mesh->engine->record);
    bl_Material material = l_meshMaterial(source);
    L_Record* materialRecord = l_peek(r, material._id);
    if (materialRecord)
    {
        ++((L_Material*)materialRecord)->meshReferences;
    }
    *out = cloned;
    return BL_OK;
}
