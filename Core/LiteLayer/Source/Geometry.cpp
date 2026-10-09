#include "LiteInternal.h"

static const uint8_t components[6] = {3, 3, 2, 2, 4, 4};
static const bgfx::Attrib::Enum attribs[6] = {bgfx::Attrib::Position,  bgfx::Attrib::Normal,
                                              bgfx::Attrib::TexCoord0, bgfx::Attrib::TexCoord1,
                                              bgfx::Attrib::Tangent,   bgfx::Attrib::Color0};

static void spans(const bl_MeshGeometry* g, bl_F32Span a[6])
{
    a[0] = g->positions;
    a[1] = g->normals;
    a[2] = g->uvs;
    a[3] = g->uvs2;
    a[4] = g->tangents;
    a[5] = g->colors;
}

static bl_Status validateGeometry(bl_Runtime* r, const bl_MeshGeometry* g)
{
    if (!g || g->positions.count % 3 || g->indices.count % 3 ||
        g->positions.count / 3 > UINT32_MAX || g->indices.count > UINT32_MAX ||
        !l_typedSpan(g->indices.data, g->indices.count, alignof(uint32_t)) ||
        g->indices.count > UINT32_MAX / sizeof(uint32_t))
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Invalid geometry counts");
    }
    bl_F32Span a[6];
    spans(g, a);
    size_t vertices = g->positions.count / 3;
    size_t stride = 0;
    for (unsigned i = 0; i < 6; ++i)
    {
        if (i < 2 || a[i].data)
        {
            stride += components[i] * sizeof(float);
        }
    }
    if (vertices > UINT32_MAX / stride)
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Vertex byte size exceeds bgfx capacity");
    }
    for (unsigned i = 0; i < 6; ++i)
    {
        if (!l_typedSpan(a[i].data, a[i].count, alignof(float)) ||
            ((i < 2 || a[i].data) && a[i].count != vertices * components[i]))
        {
            return L_FAIL(r, BL_INVALID_ARGUMENT, "Invalid attribute span");
        }
        for (size_t j = 0; j < a[i].count; ++j)
        {
            if (!isfinite(a[i].data[j]))
            {
                return L_FAIL(r, BL_INVALID_ARGUMENT, "Non-finite attribute");
            }
        }
    }
    for (size_t i = 0; i < g->indices.count; ++i)
    {
        if (g->indices.data[i] >= vertices)
        {
            return L_FAIL(r, BL_INVALID_ARGUMENT, "Index exceeds active vertices");
        }
    }
    return BL_OK;
}

void l_geometryCleanup(bl_Runtime* r, L_Geometry* g)
{
    if (bgfx::isValid(g->vertexBuffer))
    {
        bgfx::destroy(g->vertexBuffer);
    }
    if (bgfx::isValid(g->indexBuffer))
    {
        bgfx::destroy(g->indexBuffer);
    }
    for (unsigned i = 0; i < 6; ++i)
    {
        l_free(r, g->streams[i]);
    }
    l_free(r, g->indices);
    l_free(r, g->gpuVertices);
    *g = {};
    g->vertexBuffer.idx = g->indexBuffer.idx = BL_INVALID_BGFX_HANDLE;
}

static void bounds(L_Geometry* g)
{
    g->minimum = g->maximum = {};
    if (!g->vertices)
    {
        return;
    }
    float* p = g->streams[0];
    g->minimum = g->maximum = {p[0], p[1], p[2]};
    for (size_t i = 1; i < g->vertices; ++i)
    {
        double x = p[i * 3];
        double y = p[i * 3 + 1];
        double z = p[i * 3 + 2];
        if (x < g->minimum.x)
        {
            g->minimum.x = x;
        }
        if (x > g->maximum.x)
        {
            g->maximum.x = x;
        }
        if (y < g->minimum.y)
        {
            g->minimum.y = y;
        }
        if (y > g->maximum.y)
        {
            g->maximum.y = y;
        }
        if (z < g->minimum.z)
        {
            g->minimum.z = z;
        }
        if (z > g->maximum.z)
        {
            g->maximum.z = z;
        }
    }
}

static bl_Status cpuGeometry(bl_Runtime* r, const bl_MeshGeometry* input, size_t vc, size_t ic,
                             L_Geometry* g)
{
    *g = {};
    g->vertexBuffer.idx = g->indexBuffer.idx = BL_INVALID_BGFX_HANDLE;
    bl_F32Span a[6];
    spans(input, a);
    g->vertices = input->positions.count / 3;
    g->indexCount = input->indices.count;
    g->vertexCapacity = vc;
    g->indexCapacity = ic;
    for (unsigned i = 0; i < 6; ++i)
    {
        if (i < 2 || a[i].data)
        {
            g->attributeMask |= 1u << i;
            size_t bytes;
            if (!l_size(vc, components[i] * sizeof(float), &bytes) || bytes > UINT32_MAX)
            {
                l_geometryCleanup(r, g);
                return BL_INVALID_ARGUMENT;
            }
            g->streams[i] = (float*)l_alloc(r, bytes);
            if (bytes && !g->streams[i])
            {
                l_geometryCleanup(r, g);
                return BL_OUT_OF_MEMORY;
            }
            if (a[i].count)
            {
                memcpy(g->streams[i], a[i].data, a[i].count * sizeof(float));
            }
            g->offsets[i] = g->stride;
            g->stride += components[i] * sizeof(float);
        }
    }
    size_t bytes;
    if (!l_size(ic, sizeof(uint32_t), &bytes) || bytes > UINT32_MAX ||
        (vc && vc > UINT32_MAX / g->stride))
    {
        l_geometryCleanup(r, g);
        return BL_INVALID_ARGUMENT;
    }
    g->indices = (uint32_t*)l_alloc(r, bytes);
    if (bytes && !g->indices)
    {
        l_geometryCleanup(r, g);
        return BL_OUT_OF_MEMORY;
    }
    if (input->indices.count)
    {
        memcpy(g->indices, input->indices.data, input->indices.count * sizeof(uint32_t));
    }
    bounds(g);
    return BL_OK;
}

static uint8_t* interleave(bl_Runtime* r, L_Geometry* g, size_t offset, size_t count)
{
    size_t bytes = count * g->stride;
    uint8_t* p = (uint8_t*)l_alloc(r, bytes);
    if (!p)
    {
        return NULL;
    }
    for (size_t v = 0; v < count; ++v)
    {
        for (unsigned i = 0; i < 6; ++i)
        {
            if (g->streams[i])
            {
                memcpy(p + v * g->stride + g->offsets[i],
                       g->streams[i] + (v + offset) * components[i], components[i] * sizeof(float));
            }
        }
    }
    return p;
}

static bl_Status gpuGeometry(bl_Runtime* r, L_Geometry* g)
{
    if (!g->vertexCapacity)
    {
        return BL_OK;
    }
    uint8_t* p = interleave(r, g, 0, g->vertexCapacity);
    if (!p)
    {
        return BL_OUT_OF_MEMORY;
    }
    const bgfx::Memory* v = bgfx::copy(p, (uint32_t)(g->vertexCapacity * g->stride));
    g->gpuVertices = p;
    bgfx::VertexLayout layout;
    layout.begin();
    for (unsigned i = 0; i < 6; ++i)
    {
        if (g->attributeMask & (1u << i))
        {
            layout.add(attribs[i], components[i], bgfx::AttribType::Float);
        }
    }
    layout.end();
    g->vertexBuffer = bgfx::createDynamicVertexBuffer(v, layout);
    if (g->indexCapacity)
    {
        g->indexBuffer = bgfx::createDynamicIndexBuffer(
            bgfx::copy(g->indices, (uint32_t)(g->indexCapacity * sizeof(uint32_t))),
            BGFX_BUFFER_INDEX32);
    }
    if (!bgfx::isValid(g->vertexBuffer) || (g->indexCapacity && !bgfx::isValid(g->indexBuffer)))
    {
        return L_FAIL(r, BL_OUT_OF_MEMORY, "GPU geometry allocation failed");
    }
    return BL_OK;
}

void l_meshCleanup(bl_Runtime* r, L_Record* record)
{
    L_Mesh* m = (L_Mesh*)record;
    bl_Material material = l_meshMaterial(m);
    if (!L_NULL(material))
    {
        L_Material* mat = (L_Material*)l_peek(r, material._id);
        if (mat && mat->meshReferences)
        {
            --mat->meshReferences;
        }
    }
    l_geometryCleanup(r, &m->geometry);
    l_cleanNode(r, &m->node);
    l_unpin(&m->engine->record);
    record->disposed = true;
}

bl_Status bl_createMeshFromData(bl_EngineContext h, bl_String name, const bl_MeshGeometry* g,
                                bl_Mesh* out)
{
    L_GET(h, L_ENGINE, L_Engine, e);
    bl_Runtime* r = h._runtime;
    if (!out || !l_string(name))
    {
        return BL_INVALID_ARGUMENT;
    }
    L_TRY(validateGeometry(r, g));
    L_Geometry geometry;
    L_TRY(cpuGeometry(r, g, g->positions.count / 3, g->indices.count, &geometry));
    bl_Status status = gpuGeometry(r, &geometry);
    if (status != BL_OK)
    {
        l_geometryCleanup(r, &geometry);
        return status;
    }
    bl_String copy = l_copyString(r, name);
    if (!copy.data)
    {
        l_geometryCleanup(r, &geometry);
        return BL_OUT_OF_MEMORY;
    }
    L_Record* record;
    status = l_record(r, sizeof(L_Mesh), L_MESH, 30, l_meshCleanup, &record);
    if (status != BL_OK)
    {
        l_geometryCleanup(r, &geometry);
        l_free(r, (void*)copy.data);
        return status;
    }
    L_Mesh* m = (L_Mesh*)record;
    l_initNode(&m->node);
    m->node.highPrecision = e->highPrecision;
    m->node.name = copy;
    m->engine = e;
    m->geometry = geometry;
    l_pin(&e->record);
    *out = {r, m->node.record.id};
    return BL_OK;
}

bl_Status bl_createBox(bl_EngineContext e, const bl_BoxOptions* o, bl_Mesh* out)
{
    L_GET(e, L_ENGINE, L_Engine, engine);
    (void)engine;
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    bl_GeometryData d;
    L_TRY(bl_createBoxData(e._runtime, o, &d));
    bl_MeshGeometry g = {{d.positions, d.vertexCount * 3},
                         {d.normals, d.vertexCount * 3},
                         {d.indices, d.indexCount},
                         {d.uvs, d.vertexCount * 2},
                         {},
                         {},
                         {}};
    bl_Status s = bl_createMeshFromData(e, {"box", 3}, &g, out);
    bl_freeGeometryData(e._runtime, &d);
    return s;
}

bl_Status bl_createSphere(bl_EngineContext e, const bl_SphereOptions* o, bl_Mesh* out)
{
    L_GET(e, L_ENGINE, L_Engine, engine);
    (void)engine;
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    bl_GeometryData d;
    L_TRY(bl_createSphereData(e._runtime, o, &d));
    bl_MeshGeometry g = {{d.positions, d.vertexCount * 3},
                         {d.normals, d.vertexCount * 3},
                         {d.indices, d.indexCount},
                         {d.uvs, d.vertexCount * 2},
                         {},
                         {},
                         {}};
    bl_Status s = bl_createMeshFromData(e, {"sphere", 6}, &g, out);
    bl_freeGeometryData(e._runtime, &d);
    return s;
}

bl_Status bl_createGround(bl_EngineContext e, const bl_GroundOptions* o, bl_Mesh* out)
{
    L_GET(e, L_ENGINE, L_Engine, engine);
    (void)engine;
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    size_t vertices;
    size_t indices;
    L_TRY(l_groundCounts(e._runtime, o, true, &vertices, &indices));
    bl_GeometryData data;
    L_TRY(bl_createFlatGroundData(e._runtime, o, &data));
    bl_MeshGeometry geometry = {{data.positions, vertices * 3},
                                {data.normals, vertices * 3},
                                {data.indices, indices},
                                {data.uvs, vertices * 2},
                                {},
                                {},
                                {}};
    bl_Status status = bl_createMeshFromData(e, {"ground", 6}, &geometry, out);
    bl_freeGeometryData(e._runtime, &data);
    return status;
}

bl_Status bl_getMeshProperties(bl_Mesh h, bl_MeshProperties* out)
{
    L_GET(h, L_MESH, L_Mesh, m);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    if (!L_NULL(m->material) && h._runtime->slots[(uint32_t)m->material._id - 1].kind == L_STANDARD)
    {
        return BL_UNSUPPORTED;
    }
    *out = m->properties;
    return BL_OK;
}

bl_Status bl_setMeshProperties(bl_Mesh h, const bl_MeshProperties* p)
{
    L_GET(h, L_MESH, L_Mesh, mesh);
    (void)mesh;
    if (!p || (p->renderOrder.present && !isfinite(p->renderOrder.value)))
    {
        return BL_INVALID_ARGUMENT;
    }
    if (p->receiveShadows)
    {
        return L_FAIL(h._runtime, BL_UNSUPPORTED, "Shadows are outside this scope");
    }
    if (!L_NULL(p->material))
    {
        if (p->material._runtime != h._runtime)
        {
            return BL_WRONG_RUNTIME;
        }
        L_Record* record;
        L_TRY(l_get(h._runtime, p->material._id, L_MATERIAL, &record));
    }
    bl_MeshProperties2 properties = {
        {p->material._runtime, p->material._id}, p->renderOrder, p->receiveShadows};
    return bl_setMeshProperties2(h, &properties);
}

bl_Material l_meshMaterial(const L_Mesh* mesh)
{
    return L_NULL(mesh->material)
               ? bl_Material{mesh->properties.material._runtime, mesh->properties.material._id}
               : mesh->material;
}

bl_Status l_checkStandardGeometry(bl_Runtime* r, const L_Material* material, const L_Mesh* mesh)
{
    if (material->record.kind == L_STANDARD && (mesh->geometry.attributeMask & (1u << 5)))
    {
        return L_FAIL(r, BL_UNSUPPORTED, "Standard automatic RGB vertex color is not supported");
    }
    return BL_OK;
}

bl_Status bl_getMeshProperties2(bl_Mesh h, bl_MeshProperties2* out)
{
    L_GET(h, L_MESH, L_Mesh, m);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = {l_meshMaterial(m), m->properties.renderOrder, m->properties.receiveShadows};
    return BL_OK;
}

bl_Status bl_setMeshProperties2(bl_Mesh h, const bl_MeshProperties2* p)
{
    L_GET(h, L_MESH, L_Mesh, m);
    bl_Runtime* r = h._runtime;
    if (!p || (p->renderOrder.present && !isfinite(p->renderOrder.value)))
    {
        return BL_INVALID_ARGUMENT;
    }
    if (p->receiveShadows)
    {
        return L_FAIL(r, BL_UNSUPPORTED, "Shadows are outside this scope");
    }
    L_Material* next = NULL;
    if (!L_NULL(p->material))
    {
        if (p->material._runtime != r)
        {
            return BL_WRONG_RUNTIME;
        }
        L_TRY(l_materialFamily(p->material, &next));
        if (next->engine && next->engine != m->engine)
        {
            return BL_WRONG_ENGINE;
        }
        bool registered = false;
        for (L_Scene* s = m->engine->scenes; s && !registered; s = s->next)
        {
            for (size_t i = 0; i < s->memberCount; ++i)
            {
                if (s->members[i] == &m->node)
                {
                    registered = true;
                    break;
                }
            }
        }
        if (registered)
        {
            L_TRY(l_checkStandardGeometry(r, next, m));
            L_TRY(l_prepareMaterial(r, m->engine, next));
        }
    }
    bl_Material current = l_meshMaterial(m);
    if (current._id != p->material._id)
    {
        L_Record* oldRecord = l_peek(r, current._id);
        bool oldStandard = oldRecord && oldRecord->kind == L_STANDARD;
        bool newStandard = next && next->record.kind == L_STANDARD;
        if (oldStandard != newStandard)
        {
            for (size_t i = 0; i < r->count; ++i)
            {
                L_Record* record = r->records[i];
                if (!record || record->disposed || record->kind != L_SCENE)
                {
                    continue;
                }
                L_Scene* scene = (L_Scene*)record;
                for (size_t j = 0; j < scene->memberCount; ++j)
                {
                    if (scene->members[j] == &m->node)
                    {
                        if (newStandard)
                        {
                            ++scene->standardMemberCount;
                        }
                        else if (scene->standardMemberCount)
                        {
                            --scene->standardMemberCount;
                        }
                    }
                }
            }
        }
        if (!L_NULL(current))
        {
            L_Material* old = (L_Material*)l_peek(r, current._id);
            if (old && old->meshReferences)
            {
                --old->meshReferences;
            }
        }
        if (next)
        {
            ++next->meshReferences;
        }
    }
    m->material = p->material;
    m->properties.material = next && next->record.kind == L_MATERIAL
                                 ? bl_ShaderMaterial{r, next->record.id}
                                 : bl_ShaderMaterial{};
    m->properties.renderOrder = p->renderOrder;
    m->properties.receiveShadows = p->receiveShadows;
    return BL_OK;
}

static bl_Status meshForEngine(bl_EngineContext h, bl_Mesh mesh, L_Mesh** out)
{
    L_GET(h, L_ENGINE, L_Engine, e);
    if (mesh._runtime != h._runtime)
    {
        return BL_WRONG_RUNTIME;
    }
    L_GET(mesh, L_MESH, L_Mesh, m);
    if (m->engine != e)
    {
        return BL_WRONG_ENGINE;
    }
    *out = m;
    return BL_OK;
}

static bool sameLayout(L_Geometry* g, const bl_MeshGeometry* input)
{
    bl_F32Span a[6];
    spans(input, a);
    for (unsigned i = 2; i < 6; ++i)
    {
        if (((g->attributeMask & (1u << i)) != 0) != (a[i].data != NULL))
        {
            return false;
        }
    }
    return true;
}

static bl_Status uploadGeometry(bl_Runtime* r, L_Geometry* old, L_Geometry* next,
                                const bl_GeometryUpdateRanges* ranges)
{
    if (!bgfx::isValid(old->vertexBuffer))
    {
        return BL_OK;
    }
    bl_GeometryRange allV = {0, next->vertices};
    bl_GeometryRange allI = {0, next->indexCapacity};
    const bl_GeometryRange* v = ranges ? ranges->vertices : &allV;
    const bl_GeometryRange* ix = ranges ? ranges->indices : &allI;
    size_t vn = ranges ? ranges->vertexRangeCount : 1;
    size_t in = ranges ? ranges->indexRangeCount : 1;
    size_t mirrorBytes = old->vertexCapacity * old->stride;
    next->gpuVertices = (uint8_t*)l_alloc(r, mirrorBytes);
    if (mirrorBytes && !next->gpuVertices)
    {
        return BL_OUT_OF_MEMORY;
    }
    if (mirrorBytes)
    {
        memcpy(next->gpuVertices, old->gpuVertices, mirrorBytes);
    }
    size_t total = 0;
    for (size_t i = 0; i < vn; ++i)
    {
        size_t bytes = v[i].count * next->stride;
        if (bytes > SIZE_MAX - total)
        {
            return BL_INVALID_ARGUMENT;
        }
        total += bytes;
    }
    uint8_t* pack = (uint8_t*)l_alloc(r, total);
    if (total && !pack)
    {
        return BL_OUT_OF_MEMORY;
    }
    size_t offset = 0;
    for (size_t k = 0; k < vn; ++k)
    {
        for (size_t j = 0; j < v[k].count; ++j)
        {
            for (unsigned i = 0; i < 6; ++i)
            {
                if (next->streams[i])
                {
                    memcpy(pack + offset + j * next->stride + next->offsets[i],
                           next->streams[i] + (v[k].offset + j) * components[i],
                           components[i] * sizeof(float));
                }
            }
        }
        offset += v[k].count * next->stride;
    }
    offset = 0;
    for (size_t i = 0; i < vn; ++i)
    {
        if (v[i].count)
        {
            uint32_t bytes = (uint32_t)(v[i].count * next->stride);
            memcpy(next->gpuVertices + v[i].offset * next->stride, pack + offset, bytes);
            bgfx::update(old->vertexBuffer, (uint32_t)v[i].offset,
                         bgfx::copy(pack + offset, bytes));
            offset += bytes;
        }
    }
    for (size_t i = 0; i < in; ++i)
    {
        if (ix[i].count)
        {
            bgfx::update(old->indexBuffer, (uint32_t)ix[i].offset,
                         bgfx::copy(next->indices + ix[i].offset,
                                    (uint32_t)(ix[i].count * sizeof(uint32_t))));
        }
    }
    if (ranges && next->indexCount < old->indexCount)
    {
        // Shrinking always retires the old triangle tail, even when no explicit range uploads.
        size_t removed = old->indexCount - next->indexCount;
        bgfx::update(
            old->indexBuffer, (uint32_t)next->indexCount,
            bgfx::copy(next->indices + next->indexCount, (uint32_t)(removed * sizeof(uint32_t))));
    }
    l_free(r, pack);
    return BL_OK;
}

static bl_Status replaceGeometry(bl_EngineContext h, bl_Mesh mesh, const bl_MeshGeometry* g,
                                 unsigned mode, const bl_OptionalNumber* reserve,
                                 const bl_GeometryUpdateRanges* ranges,
                                 bl_GeometryCapacityResult* out)
{
    L_Mesh* m;
    L_TRY(meshForEngine(h, mesh, &m));
    bl_Runtime* r = h._runtime;
    L_TRY(validateGeometry(r, g));
    L_Geometry* old = &m->geometry;
    size_t vc = g->positions.count / 3;
    size_t ic = g->indices.count;
    double factor = reserve && reserve->present ? reserve->value : 1.25;
    if (mode == 2 && (!out || !isfinite(factor) || factor < 1))
    {
        return BL_INVALID_ARGUMENT;
    }
    if (!mode && (vc != old->vertices || ic != old->indexCount || !sameLayout(old, g)))
    {
        return BL_INVALID_ARGUMENT;
    }
    if (ranges)
    {
        if (!l_typedSpan(ranges->vertices, ranges->vertexRangeCount, alignof(bl_GeometryRange)) ||
            !l_typedSpan(ranges->indices, ranges->indexRangeCount, alignof(bl_GeometryRange)))
        {
            return BL_INVALID_ARGUMENT;
        }
        for (size_t i = 0; i < ranges->vertexRangeCount; ++i)
        {
            if (ranges->vertices[i].offset > vc ||
                ranges->vertices[i].count > vc - ranges->vertices[i].offset)
            {
                return BL_INVALID_ARGUMENT;
            }
        }
        for (size_t i = 0; i < ranges->indexRangeCount; ++i)
        {
            if (ranges->indices[i].offset > ic ||
                ranges->indices[i].count > ic - ranges->indices[i].offset)
            {
                return BL_INVALID_ARGUMENT;
            }
        }
    }
    bool stable =
        mode != 1 && vc <= old->vertexCapacity && ic <= old->indexCapacity && sameLayout(old, g);
    size_t vcap = stable ? old->vertexCapacity : vc;
    size_t icap = stable ? old->indexCapacity : ic;
    if (mode == 2 && !stable)
    {
        double v = ceil((double)vc * factor);
        double i = ceil(ceil((double)ic * factor) / 3) * 3;
        if (v > UINT32_MAX || i > UINT32_MAX)
        {
            return BL_INVALID_ARGUMENT;
        }
        vcap = (size_t)v;
        icap = (size_t)i;
    }
    L_Geometry next;
    L_TRY(cpuGeometry(r, g, vcap, icap, &next));
    bl_Status s = stable ? uploadGeometry(r, old, &next, ranges) : gpuGeometry(r, &next);
    if (s != BL_OK)
    {
        l_geometryCleanup(r, &next);
        return s;
    }
    if (stable)
    {
        next.vertexBuffer = old->vertexBuffer;
        next.indexBuffer = old->indexBuffer;
        old->vertexBuffer.idx = old->indexBuffer.idx = BL_INVALID_BGFX_HANDLE;
    }
    l_geometryCleanup(r, old);
    *old = next;
    if (out)
    {
        *out = {stable, vcap, icap};
    }
    return BL_OK;
}

bl_Status bl_updateMeshGeometry(bl_EngineContext e, bl_Mesh m, const bl_MeshGeometry* g)
{
    return replaceGeometry(e, m, g, 0, NULL, NULL, NULL);
}

bl_Status bl_resizeMeshGeometry(bl_EngineContext e, bl_Mesh m, const bl_MeshGeometry* g)
{
    return replaceGeometry(e, m, g, 1, NULL, NULL, NULL);
}

bl_Status bl_updateMeshGeometryCapacity(bl_EngineContext e, bl_Mesh m, const bl_MeshGeometry* g,
                                        const bl_OptionalNumber* reserve,
                                        const bl_GeometryUpdateRanges* ranges,
                                        bl_GeometryCapacityResult* out)
{
    return replaceGeometry(e, m, g, 2, reserve, ranges, out);
}

static bl_Status attributeUpdate(bl_EngineContext h, bl_Mesh mesh, bl_F32Span values, size_t offset,
                                 const size_t* count, size_t source, unsigned attr)
{
    L_Mesh* m;
    L_TRY(meshForEngine(h, mesh, &m));
    L_Geometry* g = &m->geometry;
    if (!(g->attributeMask & (1u << attr)) ||
        !l_typedSpan(values.data, values.count, alignof(float)) ||
        values.count % components[attr] || source > values.count / components[attr])
    {
        return BL_INVALID_ARGUMENT;
    }
    size_t n = count ? *count : values.count / components[attr] - source;
    if (offset > g->vertices || n > g->vertices - offset ||
        n > values.count / components[attr] - source)
    {
        return BL_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < n * components[attr]; ++i)
    {
        if (!isfinite(values.data[source * components[attr] + i]))
        {
            return BL_INVALID_ARGUMENT;
        }
    }
    if (!n)
    {
        return BL_OK;
    }
    if (!bgfx::isValid(g->vertexBuffer))
    {
        return BL_NOT_READY;
    }
    uint8_t* p = (uint8_t*)l_alloc(h._runtime, n * g->stride);
    if (!p)
    {
        return BL_OUT_OF_MEMORY;
    }
    memcpy(p, g->gpuVertices + offset * g->stride, n * g->stride);
    for (size_t v = 0; v < n; ++v)
    {
        memcpy(p + v * g->stride + g->offsets[attr], values.data + (source + v) * components[attr],
               components[attr] * sizeof(float));
    }
    memcpy(g->gpuVertices + offset * g->stride, p, n * g->stride);
    bgfx::update(g->vertexBuffer, (uint32_t)offset, bgfx::copy(p, (uint32_t)(n * g->stride)));
    l_free(h._runtime, p);
    return BL_OK;
}

bl_Status bl_updateMeshPositions(bl_EngineContext e, bl_Mesh m, bl_F32Span v, size_t o,
                                 const size_t* n, size_t s)
{
    return attributeUpdate(e, m, v, o, n, s, 0);
}

bl_Status bl_updateMeshNormals(bl_EngineContext e, bl_Mesh m, bl_F32Span v, size_t o,
                               const size_t* n, size_t s)
{
    return attributeUpdate(e, m, v, o, n, s, 1);
}

bl_Status bl_updateMeshUvs(bl_EngineContext e, bl_Mesh m, bl_F32Span v, size_t o, const size_t* n,
                           size_t s)
{
    return attributeUpdate(e, m, v, o, n, s, 2);
}

bl_Status bl_updateMeshUv2(bl_EngineContext e, bl_Mesh m, bl_F32Span v, size_t o, const size_t* n,
                           size_t s)
{
    return attributeUpdate(e, m, v, o, n, s, 3);
}

bl_Status bl_updateMeshTangents(bl_EngineContext e, bl_Mesh m, bl_F32Span v, size_t o,
                                const size_t* n, size_t s)
{
    return attributeUpdate(e, m, v, o, n, s, 4);
}

bl_Status bl_updateMeshColors(bl_EngineContext e, bl_Mesh m, bl_F32Span v, size_t o,
                              const size_t* n, size_t s)
{
    return attributeUpdate(e, m, v, o, n, s, 5);
}

bl_Status bl_invalidateRenderBundles(bl_EngineContext h)
{
    L_GET(h, L_ENGINE, L_Engine, e);
    (void)e;
    return BL_OK;
}
