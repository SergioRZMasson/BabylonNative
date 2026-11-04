/* Original mesh/create-line-system.ts, Lite 1.32.0, 2e064d88 (Apache-2.0). */
#include "LiteInternal.h"

struct L_LineWork
{
    float* positions;
    float* normals;
    float* colors;
    uint32_t* indices;
    uint32_t* pointCounts;
    size_t vertices;
    size_t indexCount;
};

static void freeWork(bl_Runtime* r, L_LineWork* work)
{
    l_free(r, work->positions);
    l_free(r, work->normals);
    l_free(r, work->colors);
    l_free(r, work->indices);
    l_free(r, work->pointCounts);
}

static bl_Status lineWork(bl_Runtime* r, bl_Vec3PathSpan lines, bool hasColors,
                          bl_Color4PathSpan colors, L_LineWork* out)
{
    if (!l_typedSpan(lines.data, lines.count, alignof(bl_Vec3Span)) ||
        (hasColors && (!l_typedSpan(colors.data, colors.count, alignof(bl_Color4Span)) ||
                       colors.count != lines.count)))
    {
        return BL_INVALID_ARGUMENT;
    }
    L_LineWork work = {};
    for (size_t i = 0; i < lines.count; ++i)
    {
        const bl_Vec3Span* line = lines.data + i;
        if (!l_typedSpan(line->data, line->count, alignof(bl_Vec3)) || line->count > UINT32_MAX ||
            work.vertices > UINT32_MAX - line->count ||
            (hasColors &&
             (colors.data[i].count != line->count ||
              !l_typedSpan(colors.data[i].data, colors.data[i].count, alignof(bl_Color4)))))
        {
            return BL_INVALID_ARGUMENT;
        }
        work.vertices += line->count;
        size_t segments = line->count ? line->count - 1 : 0;
        if (segments > (UINT32_MAX - work.indexCount) / 2)
        {
            return BL_INVALID_ARGUMENT;
        }
        work.indexCount += segments * 2;
        for (size_t j = 0; j < line->count; ++j)
        {
            bl_Vec3 p = line->data[j];
            if (!l_vec(p) || !isfinite((float)p.x) || !isfinite((float)p.y) ||
                !isfinite((float)p.z))
            {
                return BL_INVALID_ARGUMENT;
            }
            if (hasColors)
            {
                bl_Color4 c = colors.data[i].data[j];
                if (!isfinite(c.r) || !isfinite(c.g) || !isfinite(c.b) || !isfinite(c.a) ||
                    !isfinite((float)c.r) || !isfinite((float)c.g) || !isfinite((float)c.b) ||
                    !isfinite((float)c.a))
                {
                    return BL_INVALID_ARGUMENT;
                }
            }
        }
    }
    if (!work.vertices || work.vertices > UINT32_MAX / 40 ||
        work.indexCount > UINT32_MAX / sizeof(uint32_t) ||
        lines.count > SIZE_MAX / sizeof(uint32_t))
    {
        return BL_INVALID_ARGUMENT;
    }
    work.positions = (float*)l_alloc(r, work.vertices * 3 * sizeof(float));
    work.normals = (float*)l_alloc(r, work.vertices * 3 * sizeof(float));
    work.colors = hasColors ? (float*)l_alloc(r, work.vertices * 4 * sizeof(float)) : NULL;
    work.indices = (uint32_t*)l_alloc(r, work.indexCount * sizeof(uint32_t));
    work.pointCounts = (uint32_t*)l_alloc(r, lines.count * sizeof(uint32_t));
    if (!work.positions || !work.normals || (hasColors && !work.colors) ||
        (work.indexCount && !work.indices) || (lines.count && !work.pointCounts))
    {
        freeWork(r, &work);
        return BL_OUT_OF_MEMORY;
    }
    memset(work.normals, 0, work.vertices * 3 * sizeof(float));
    size_t vertex = 0;
    size_t index = 0;
    for (size_t i = 0; i < lines.count; ++i)
    {
        work.pointCounts[i] = (uint32_t)lines.data[i].count;
        for (size_t j = 0; j < lines.data[i].count; ++j, ++vertex)
        {
            bl_Vec3 p = lines.data[i].data[j];
            work.positions[vertex * 3] = (float)p.x;
            work.positions[vertex * 3 + 1] = (float)p.y;
            work.positions[vertex * 3 + 2] = (float)p.z;
            if (hasColors)
            {
                bl_Color4 c = colors.data[i].data[j];
                work.colors[vertex * 4] = (float)c.r;
                work.colors[vertex * 4 + 1] = (float)c.g;
                work.colors[vertex * 4 + 2] = (float)c.b;
                work.colors[vertex * 4 + 3] = (float)c.a;
            }
            if (j)
            {
                work.indices[index++] = (uint32_t)vertex - 1;
                work.indices[index++] = (uint32_t)vertex;
            }
        }
    }
    *out = work;
    return BL_OK;
}

bl_Status bl_createLineSystem(bl_EngineContext h, const bl_LineSystemOptions* o, bl_Mesh* out)
{
    L_GET(h, L_ENGINE, L_Engine, engine);
    (void)engine;
    if (!out || !o || !l_string(o->name))
    {
        return BL_INVALID_ARGUMENT;
    }
    bl_Runtime* r = h._runtime;
    L_Material* supplied = NULL;
    if (!L_NULL(o->material))
    {
        if (o->material._runtime != r)
        {
            return BL_WRONG_RUNTIME;
        }
        L_Record* record;
        L_TRY(l_get(r, o->material._id, L_MATERIAL, &record));
        supplied = (L_Material*)record;
        if (!supplied->lineMaterial || supplied->lineVertexColor != o->hasColors)
        {
            return l_error(r, BL_INVALID_ARGUMENT, __func__, "Line material colour layout mismatch",
                           434);
        }
    }
    L_LineWork work;
    L_TRY(lineWork(r, o->lines, o->hasColors, o->colors, &work));
    bl_ShaderMaterial material = o->material;
    bl_Status status = BL_OK;
    if (!supplied)
    {
        const bl_LineMaterialOptions options = {{},
                                                o->hasColor,
                                                o->color,
                                                o->hasColors ? BL_BOOL_TRUE : BL_BOOL_FALSE,
                                                o->useVertexAlpha,
                                                o->useThinInstances,
                                                o->useThinInstanceColors,
                                                BL_BOOL_DEFAULT,
                                                BL_COMPARE_DEFAULT};
        status = bl_createLineMaterial(r, &options, &material);
    }
    bl_Mesh mesh = {};
    if (status == BL_OK)
    {
        const bl_MeshGeometry geometry = {{work.positions, work.vertices * 3},
                                          {work.normals, work.vertices * 3},
                                          {work.indices, work.indexCount},
                                          {},
                                          {},
                                          {},
                                          {work.colors, o->hasColors ? work.vertices * 4 : 0}};
        status = l_createMeshTopology(h, o->name.data ? o->name : bl_String{"lineSystem", 10},
                                      &geometry, BL_TOPOLOGY_LINE_LIST, &mesh);
    }
    if (status == BL_OK)
    {
        bl_MeshProperties properties = {};
        properties.material = material;
        status = bl_setMeshProperties(mesh, &properties);
    }
    if (status == BL_OK)
    {
        L_Mesh* record = (L_Mesh*)l_peek(r, mesh._id);
        record->linePointCounts = work.pointCounts;
        record->lineCount = o->lines.count;
        work.pointCounts = NULL;
        *out = mesh;
    }
    else
    {
        if (!L_NULL(mesh))
        {
            bl_disposeNode({r, mesh._id});
        }
        if (!supplied && !L_NULL(material))
        {
            bl_disposeShaderMaterial(material);
        }
    }
    freeWork(r, &work);
    return status;
}

bl_Status bl_updateLineSystem(bl_EngineContext h, bl_Mesh handle,
                              const bl_LineSystemUpdateOptions* o)
{
    L_GET(h, L_ENGINE, L_Engine, engine);
    if (handle._runtime != h._runtime)
    {
        return BL_WRONG_RUNTIME;
    }
    L_GET(handle, L_MESH, L_Mesh, mesh);
    if (mesh->engine != engine)
    {
        return BL_WRONG_ENGINE;
    }
    if (!o || !mesh->linePointCounts || o->lines.count != mesh->lineCount ||
        !l_typedSpan(o->lines.data, o->lines.count, alignof(bl_Vec3Span)) ||
        (o->hasColors && !mesh->geometry.streams[5]))
    {
        return BL_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < mesh->lineCount; ++i)
    {
        if (o->lines.data[i].count != mesh->linePointCounts[i])
        {
            return BL_INVALID_ARGUMENT;
        }
    }
    L_LineWork work;
    L_TRY(lineWork(h._runtime, o->lines, o->hasColors, o->colors, &work));
    const L_Geometry* g = &mesh->geometry;
    bl_MeshGeometry geometry = {
        {work.positions, work.vertices * 3},
        {g->streams[1], work.vertices * 3},
        {g->indices, g->indexCount},
        {},
        {},
        {},
        {o->hasColors ? work.colors : g->streams[5], g->streams[5] ? work.vertices * 4 : 0}};
    const bl_GeometryRange vertices = {0, work.vertices};
    const bl_GeometryUpdateRanges ranges = {&vertices, 1, NULL, 0};
    const bl_OptionalNumber reserve = {true, 1};
    bl_GeometryCapacityResult result;
    bl_Status status =
        bl_updateMeshGeometryCapacity(h, handle, &geometry, &reserve, &ranges, &result);
    freeWork(h._runtime, &work);
    return status;
}
