#include "LiteInternal.h"

static void dataCleanup(bl_Runtime* r, L_Record* p)
{
    L_Data* d = (L_Data*)p;
    l_free(r, d->data.positions);
    l_free(r, d->data.normals);
    l_free(r, d->data.uvs);
    l_free(r, d->data.indices);
    d->data = {};
}

static bl_Status createData(bl_Runtime* r, size_t vertices, size_t indices, L_Data** out)
{
    size_t pbytes;
    size_t ubytes;
    size_t ibytes;
    if (vertices > UINT32_MAX || !l_size(vertices, 3 * sizeof(float), &pbytes) ||
        !l_size(vertices, 2 * sizeof(float), &ubytes) ||
        !l_size(indices, sizeof(uint32_t), &ibytes))
    {
        return BL_INVALID_ARGUMENT;
    }
    L_NEW(r, L_DATA, 30, dataCleanup, L_Data, d);
    d->data.positions = (float*)l_alloc(r, pbytes);
    d->data.normals = (float*)l_alloc(r, pbytes);
    d->data.uvs = (float*)l_alloc(r, ubytes);
    d->data.indices = (uint32_t*)l_alloc(r, ibytes);
    if (!d->data.positions || !d->data.normals || !d->data.uvs || !d->data.indices)
    {
        dataCleanup(r, &d->record);
        d->record.disposed = true;
        return L_FAIL(r, BL_OUT_OF_MEMORY, "Geometry allocation failed");
    }
    if (d->record.serial > UINTPTR_MAX)
    {
        dataCleanup(r, &d->record);
        d->record.disposed = true;
        return BL_OUT_OF_MEMORY;
    }
    d->data.vertexCount = vertices;
    d->data.indexCount = indices;
    d->data.allocation = (void*)(uintptr_t)d->record.serial;
    *out = d;
    return BL_OK;
}

static double optional(bl_OptionalNumber n, double d)
{
    return n.present ? n.value : d;
}

bl_Status bl_createBoxData(bl_Runtime* r, const bl_BoxOptions* o, bl_GeometryData* out)
{
    L_TRY(l_check(r));
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    double base = o ? optional(o->size, 1) : 1;
    double dimensions[3] = {o ? optional(o->width, base) : base,
                            o ? optional(o->height, base) : base,
                            o ? optional(o->depth, base) : base};
    for (unsigned i = 0; i < 3; ++i)
    {
        if (!isfinite(dimensions[i]) || !isfinite((float)(dimensions[i] * .5)))
        {
            return BL_INVALID_ARGUMENT;
        }
    }
    L_Data* d;
    L_TRY(createData(r, 24, 36, &d));
    const uint32_t signs[3] = {UINT32_C(0x4b213fa5), UINT32_C(0xded6426f), UINT32_C(0x80)};
    const float normals[6][3] = {{0, 0, 1},  {0, 0, -1}, {1, 0, 0},
                                 {-1, 0, 0}, {0, 1, 0},  {0, -1, 0}};
    const float uv[8] = {1, 1, 0, 1, 0, 0, 1, 0};
    for (unsigned i = 0; i < 72; ++i)
    {
        d->data.positions[i] =
            (float)((((signs[i >> 5] >> (i & 31)) & 1) - .5) * dimensions[i % 3]);
    }
    for (unsigned f = 0; f < 6; ++f)
    {
        for (unsigned v = 0; v < 4; ++v)
        {
            memcpy(d->data.normals + f * 12 + v * 3, normals[f], 3 * sizeof(float));
        }
        memcpy(d->data.uvs + f * 8, uv, sizeof(uv));
        uint32_t b = f * 4;
        uint32_t ix[6] = {b, b + 1, b + 2, b, b + 2, b + 3};
        memcpy(d->data.indices + f * 6, ix, sizeof(ix));
    }
    *out = d->data;
    return BL_OK;
}

bl_Status bl_createSphereData(bl_Runtime* r, const bl_SphereOptions* o, bl_GeometryData* out)
{
    L_TRY(l_check(r));
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    double segments = o ? optional(o->segments, 32) : 32;
    double base = o ? optional(o->diameter, 1) : 1;
    double radius[3] = {o ? optional(o->diameterX, base) * .5 : base * .5,
                        o ? optional(o->diameterY, base) * .5 : base * .5,
                        o ? optional(o->diameterZ, base) * .5 : base * .5};
    if (!isfinite(segments) || floor(segments) != segments || segments > UINT32_MAX / 2 - 3)
    {
        return BL_INVALID_ARGUMENT;
    }
    for (unsigned i = 0; i < 3; ++i)
    {
        if (!isfinite(radius[i]) || !isfinite((float)radius[i]))
        {
            return BL_INVALID_ARGUMENT;
        }
    }
    if (segments < 3)
    {
        segments = 3;
    }
    size_t zCount = (size_t)segments + 2;
    size_t yCount = zCount * 2;
    size_t vertices;
    size_t indices;
    size_t q;
    if (!l_size(zCount + 1, yCount + 1, &vertices) || !l_size(zCount, yCount, &q) ||
        !l_size(q, 6, &indices))
    {
        return BL_INVALID_ARGUMENT;
    }
    L_Data* d;
    L_TRY(createData(r, vertices, indices, &d));
    size_t v = 0;
    size_t ix = 0;
    for (size_t z = 0; z <= zCount; ++z)
    {
        double nz = (double)z / (double)zCount;
        double az = nz * 3.141592653589793;
        for (size_t y = 0; y <= yCount; ++y, ++v)
        {
            double ny = (double)y / (double)yCount;
            double ay = ny * 3.141592653589793 * 2;
            double n[3] = {sin(az) * cos(ay), cos(az), -sin(az) * sin(ay)};
            for (unsigned c = 0; c < 3; ++c)
            {
                d->data.positions[v * 3 + c] = (float)(radius[c] * n[c]);
                d->data.normals[v * 3 + c] = (float)n[c];
            }
            d->data.uvs[v * 2] = (float)ny;
            d->data.uvs[v * 2 + 1] = (float)nz;
        }
    }
    for (size_t z = 0; z < zCount; ++z)
    {
        for (size_t y = 0; y < yCount; ++y)
        {
            uint32_t a = (uint32_t)(z * (yCount + 1) + y);
            uint32_t b = a + (uint32_t)yCount + 1;
            uint32_t tri[6] = {a, a + 1, b, b, a + 1, b + 1};
            memcpy(d->data.indices + ix, tri, sizeof(tri));
            ix += 6;
        }
    }
    *out = d->data;
    return BL_OK;
}

bl_Status bl_freeGeometryData(bl_Runtime* r, bl_GeometryData* data)
{
    L_TRY(l_check(r));
    if (!data)
    {
        return BL_INVALID_ARGUMENT;
    }
    if (!data->allocation)
    {
        if (data->positions || data->normals || data->uvs || data->indices)
        {
            return BL_INVALID_ARGUMENT;
        }
        *data = {};
        return BL_OK;
    }
    L_Data* d = NULL;
    for (size_t i = 0; i < r->count; ++i)
    {
        L_Record* p = r->records[i];
        if (p && p->kind == L_DATA && p->serial == (uint64_t)(uintptr_t)data->allocation)
        {
            d = (L_Data*)p;
            break;
        }
    }
    if (!d)
    {
        return L_FAIL(r, BL_INVALID_HANDLE,
                      "Geometry allocation token is not issued by this runtime");
    }
    if (!d->record.disposed)
    {
        dataCleanup(r, &d->record);
        d->record.disposed = true;
    }
    *data = {};
    return BL_OK;
}

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
    if (!L_NULL(m->properties.material))
    {
        L_Material* mat = (L_Material*)l_peek(r, m->properties.material._id);
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

bl_Status bl_getMeshProperties(bl_Mesh h, bl_MeshProperties* out)
{
    L_GET(h, L_MESH, L_Mesh, m);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = m->properties;
    return BL_OK;
}

bl_Status bl_setMeshProperties(bl_Mesh h, const bl_MeshProperties* p)
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
        L_Record* record;
        L_TRY(l_get(r, p->material._id, L_MATERIAL, &record));
        next = (L_Material*)record;
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
            L_TRY(l_prepareMaterial(r, m->engine, next));
        }
    }
    if (m->properties.material._id != p->material._id)
    {
        if (!L_NULL(m->properties.material))
        {
            L_Material* old = (L_Material*)l_peek(r, m->properties.material._id);
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
    m->properties = *p;
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
