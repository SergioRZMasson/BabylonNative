#include "RuntimeInternal.h"

struct L_Data
{
    L_Record record;
    bl_GeometryData data;
};

static_assert(__is_trivial(L_Data) && __is_standard_layout(L_Data),
              "Geometry allocation storage must be POD");
static_assert(offsetof(L_Data, record) == 0, "Geometry record must be the first member");

static void dataCleanup(bl_Runtime* r, L_Record* record)
{
    L_Data* data = (L_Data*)record;
    l_free(r, data->data.positions);
    l_free(r, data->data.normals);
    l_free(r, data->data.uvs);
    l_free(r, data->data.indices);
    data->data = {};
}

static bl_Status createData(bl_Runtime* r, size_t vertices, size_t indices, L_Data** out)
{
    size_t positionBytes;
    size_t uvBytes;
    size_t indexBytes;
    if (vertices > UINT32_MAX || !l_size(vertices, 3 * sizeof(float), &positionBytes) ||
        !l_size(vertices, 2 * sizeof(float), &uvBytes) ||
        !l_size(indices, sizeof(uint32_t), &indexBytes))
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Geometry data size overflow");
    }
    L_NEW(r, L_DATA, 30, dataCleanup, L_Data, data);
    data->data.positions = (float*)l_alloc(r, positionBytes);
    data->data.normals = (float*)l_alloc(r, positionBytes);
    data->data.uvs = (float*)l_alloc(r, uvBytes);
    data->data.indices = (uint32_t*)l_alloc(r, indexBytes);
    if (!data->data.positions || !data->data.normals || !data->data.uvs || !data->data.indices)
    {
        dataCleanup(r, &data->record);
        data->record.disposed = true;
        return L_FAIL(r, BL_OUT_OF_MEMORY, "Geometry allocation failed");
    }
    if (data->record.serial > UINTPTR_MAX)
    {
        dataCleanup(r, &data->record);
        data->record.disposed = true;
        return L_FAIL(r, BL_OUT_OF_MEMORY, "Geometry allocation token space exhausted");
    }
    data->data.vertexCount = vertices;
    data->data.indexCount = indices;
    data->data.allocation = (void*)(uintptr_t)data->record.serial;
    *out = data;
    return BL_OK;
}

static double optional(bl_OptionalNumber number, double fallback)
{
    return number.present ? number.value : fallback;
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
    L_Data* data;
    L_TRY(createData(r, 24, 36, &data));
    const uint32_t signs[3] = {UINT32_C(0x4b213fa5), UINT32_C(0xded6426f), UINT32_C(0x80)};
    const float normals[6][3] = {{0, 0, 1},  {0, 0, -1}, {1, 0, 0},
                                 {-1, 0, 0}, {0, 1, 0},  {0, -1, 0}};
    const float uv[8] = {1, 1, 0, 1, 0, 0, 1, 0};
    for (unsigned i = 0; i < 72; ++i)
    {
        data->data.positions[i] =
            (float)((((signs[i >> 5] >> (i & 31)) & 1) - .5) * dimensions[i % 3]);
    }
    for (unsigned face = 0; face < 6; ++face)
    {
        for (unsigned vertex = 0; vertex < 4; ++vertex)
        {
            memcpy(data->data.normals + face * 12 + vertex * 3, normals[face], 3 * sizeof(float));
        }
        memcpy(data->data.uvs + face * 8, uv, sizeof(uv));
        uint32_t baseVertex = face * 4;
        uint32_t indices[6] = {baseVertex, baseVertex + 1, baseVertex + 2,
                               baseVertex, baseVertex + 2, baseVertex + 3};
        memcpy(data->data.indices + face * 6, indices, sizeof(indices));
    }
    *out = data->data;
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
    size_t quads;
    if (!l_size(zCount + 1, yCount + 1, &vertices) || !l_size(zCount, yCount, &quads) ||
        !l_size(quads, 6, &indices))
    {
        return BL_INVALID_ARGUMENT;
    }
    L_Data* data;
    L_TRY(createData(r, vertices, indices, &data));
    size_t vertex = 0;
    for (size_t z = 0; z <= zCount; ++z)
    {
        double normalizedZ = (double)z / (double)zCount;
        double angleZ = normalizedZ * 3.141592653589793;
        for (size_t y = 0; y <= yCount; ++y, ++vertex)
        {
            double normalizedY = (double)y / (double)yCount;
            double angleY = normalizedY * 3.141592653589793 * 2;
            double normal[3] = {sin(angleZ) * cos(angleY), cos(angleZ), -sin(angleZ) * sin(angleY)};
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                data->data.positions[vertex * 3 + axis] = (float)(radius[axis] * normal[axis]);
                data->data.normals[vertex * 3 + axis] = (float)normal[axis];
            }
            data->data.uvs[vertex * 2] = (float)normalizedY;
            data->data.uvs[vertex * 2 + 1] = (float)normalizedZ;
        }
    }
    size_t index = 0;
    for (size_t z = 0; z < zCount; ++z)
    {
        for (size_t y = 0; y < yCount; ++y)
        {
            uint32_t a = (uint32_t)(z * (yCount + 1) + y);
            uint32_t b = a + (uint32_t)yCount + 1;
            uint32_t triangle[6] = {a, a + 1, b, b, a + 1, b + 1};
            memcpy(data->data.indices + index, triangle, sizeof(triangle));
            index += 6;
        }
    }
    *out = data->data;
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
    uint64_t serial = (uint64_t)(uintptr_t)data->allocation;
    for (size_t i = 0; i < r->count; ++i)
    {
        L_Record* record = r->records[i];
        if (!record || record->kind != L_DATA || record->serial != serial)
        {
            continue;
        }
        if (!record->disposed)
        {
            dataCleanup(r, record);
            record->disposed = true;
        }
        *data = {};
        return BL_OK;
    }
    return L_FAIL(r, BL_INVALID_HANDLE, "Geometry allocation token is not issued by this runtime");
}
