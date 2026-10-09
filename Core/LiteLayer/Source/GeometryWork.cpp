/*
 * Original Babylon Lite 1.32.0 geometry numeric stages and ComputeNormals.
 * Source 2e064d88ec7422af946f8ec7f089ac6519f99295, Apache-2.0 (LICENSE.txt).
 */
#include "GeometryDataInternal.h"
#include <float.h>

double l_geometryOption(bl_OptionalNumber option, double fallback)
{
    return option.present ? option.value : fallback;
}

double l_geometryInt32(double value)
{
    double number = fmod(trunc(value), 4294967296.0);
    if (number < 0)
    {
        number += 4294967296.0;
    }
    if (number >= 2147483648.0)
    {
        number -= 4294967296.0;
    }
    return number;
}

bool l_geometryAdd(size_t first, size_t second, size_t* out)
{
    if (first > SIZE_MAX - second)
    {
        return false;
    }
    *out = first + second;
    return true;
}

bl_Status l_geometryScratchBytes(bl_Runtime* r, size_t count, size_t size, size_t* out)
{
    if (count > UINT32_MAX || !l_size(count, size, out) || !l_allocationFits(*out))
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Procedural scratch count or byte overflow");
    }
    return BL_OK;
}

bl_Status l_geometryBudget(bl_Runtime* r, size_t vertices, size_t indices, bool gpu,
                           size_t extraScratch)
{
    size_t positionCount;
    size_t indexBytes;
    size_t total;
    if (!l_size(vertices, 3, &positionCount) || positionCount > UINT32_MAX ||
        indices > UINT32_MAX || !l_size(indices, sizeof(uint32_t), &indexBytes) ||
        !l_allocationFits(indexBytes) ||
        !l_size(vertices, 8 * (sizeof(double) + sizeof(float)), &total) ||
        !l_geometryAdd(total, indexBytes, &total) || !l_geometryAdd(total, indexBytes, &total) ||
        !l_geometryAdd(total, extraScratch, &total) || !l_allocationFits(total) ||
        (gpu && (vertices > UINT32_MAX / (8 * sizeof(float)) || indexBytes > UINT32_MAX)))
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT,
                      "Procedural source array, allocation or GPU budget overflow");
    }
    return BL_OK;
}

bl_Status l_geometryWork(bl_Runtime* r, size_t vertices, size_t indices, L_GeometryWork* out)
{
    size_t positionBytes;
    size_t uvBytes;
    size_t indexBytes;
    L_TRY(l_geometryScratchBytes(r, vertices, 3 * sizeof(double), &positionBytes));
    L_TRY(l_geometryScratchBytes(r, vertices, 2 * sizeof(double), &uvBytes));
    L_TRY(l_geometryScratchBytes(r, indices, sizeof(uint32_t), &indexBytes));
    L_GeometryWork work = {};
    work.vertices = vertices;
    work.indexCount = indices;
    work.positions = (double*)l_alloc(r, positionBytes ? positionBytes : sizeof(double));
    work.normals = (double*)l_alloc(r, positionBytes ? positionBytes : sizeof(double));
    work.uvs = (double*)l_alloc(r, uvBytes ? uvBytes : sizeof(double));
    work.indices = (uint32_t*)l_alloc(r, indexBytes ? indexBytes : sizeof(uint32_t));
    if (!work.positions || !work.normals || !work.uvs || !work.indices)
    {
        l_geometryFree(r, &work);
        return L_FAIL(r, BL_OUT_OF_MEMORY, "Procedural work allocation failed");
    }
    *out = work;
    return BL_OK;
}

void l_geometryFree(bl_Runtime* r, L_GeometryWork* work)
{
    l_free(r, work->positions);
    l_free(r, work->normals);
    l_free(r, work->uvs);
    l_free(r, work->indices);
    *work = {};
}

static bool finiteF32(double value)
{
    return isfinite(value) && fabs(value) < (double)FLT_MAX + ldexp(1.0, 103);
}

static float sourceF32(double value)
{
    // Original Float32Array rounds this half-ULP interval to finite FLT_MAX.
    // Avoid an out-of-range C++ floating conversion while retaining those bits.
    if (value > FLT_MAX)
    {
        return FLT_MAX;
    }
    if (value < -FLT_MAX)
    {
        return -FLT_MAX;
    }
    return (float)value;
}

bl_Status l_geometryFinish(bl_Runtime* r, const L_GeometryWork* work, bl_GeometryData* out)
{
    for (size_t index = 0; index < work->vertices * 3; ++index)
    {
        if (!finiteF32(work->positions[index]) || !finiteF32(work->normals[index]))
        {
            return L_FAIL(r, BL_INVALID_ARGUMENT,
                          "Original procedural position or normal is not finite F32");
        }
    }
    for (size_t index = 0; index < work->vertices * 2; ++index)
    {
        if (!finiteF32(work->uvs[index]))
        {
            return L_FAIL(r, BL_INVALID_ARGUMENT, "Original procedural UV is not finite F32");
        }
    }
    for (size_t index = 0; index < work->indexCount; ++index)
    {
        if (work->indices[index] >= work->vertices)
        {
            return L_FAIL(r, BL_INVALID_ARGUMENT, "Original procedural index exceeds vertices");
        }
    }
    bl_GeometryData data;
    L_TRY(l_createGeometryData(r, work->vertices, work->indexCount, &data));
    for (size_t index = 0; index < work->vertices * 3; ++index)
    {
        data.positions[index] = sourceF32(work->positions[index]);
        data.normals[index] = sourceF32(work->normals[index]);
    }
    for (size_t index = 0; index < work->vertices * 2; ++index)
    {
        data.uvs[index] = sourceF32(work->uvs[index]);
    }
    memcpy(data.indices, work->indices, work->indexCount * sizeof(uint32_t));
    *out = data;
    return BL_OK;
}

bl_Status l_geometryNormals(bl_Runtime* r, L_GeometryWork* work)
{
    size_t count = work->vertices * 3;
    memset(work->normals, 0, count * sizeof(double));
    for (size_t face = 0; face < work->indexCount / 3; ++face)
    {
        uint32_t first = work->indices[face * 3];
        uint32_t second = work->indices[face * 3 + 1];
        uint32_t third = work->indices[face * 3 + 2];
        if (first >= work->vertices || second >= work->vertices || third >= work->vertices)
        {
            return L_FAIL(r, BL_INVALID_ARGUMENT,
                          "Original procedural normal index is out of bounds");
        }
        size_t v1 = (size_t)first * 3;
        size_t v2 = (size_t)second * 3;
        size_t v3 = (size_t)third * 3;
        double p1p2x = work->positions[v1] - work->positions[v2];
        double p1p2y = work->positions[v1 + 1] - work->positions[v2 + 1];
        double p1p2z = work->positions[v1 + 2] - work->positions[v2 + 2];
        double p3p2x = work->positions[v3] - work->positions[v2];
        double p3p2y = work->positions[v3 + 1] - work->positions[v2 + 1];
        double p3p2z = work->positions[v3 + 2] - work->positions[v2 + 2];
        double nx = p1p2y * p3p2z - p1p2z * p3p2y;
        double ny = p1p2z * p3p2x - p1p2x * p3p2z;
        double nz = p1p2x * p3p2y - p1p2y * p3p2x;
        double length = sqrt(nx * nx + ny * ny + nz * nz);
        if (length == 0)
        {
            length = 1;
        }
        nx /= length;
        ny /= length;
        nz /= length;
        work->normals[v1] = work->normals[v1] + nx;
        work->normals[v1 + 1] = work->normals[v1 + 1] + ny;
        work->normals[v1 + 2] = work->normals[v1 + 2] + nz;
        work->normals[v2] = work->normals[v2] + nx;
        work->normals[v2 + 1] = work->normals[v2 + 1] + ny;
        work->normals[v2 + 2] = work->normals[v2 + 2] + nz;
        work->normals[v3] = work->normals[v3] + nx;
        work->normals[v3 + 1] = work->normals[v3 + 1] + ny;
        work->normals[v3 + 2] = work->normals[v3 + 2] + nz;
    }
    for (size_t vertex = 0; vertex < work->vertices; ++vertex)
    {
        double x = work->normals[vertex * 3];
        double y = work->normals[vertex * 3 + 1];
        double z = work->normals[vertex * 3 + 2];
        double length = sqrt(x * x + y * y + z * z);
        if (length == 0)
        {
            length = 1;
        }
        work->normals[vertex * 3] = x / length;
        work->normals[vertex * 3 + 1] = y / length;
        work->normals[vertex * 3 + 2] = z / length;
    }
    return BL_OK;
}

bl_Status l_geometryPoints(bl_Runtime* r, bl_Vec3Span points)
{
    size_t bytes;
    L_TRY(l_geometryScratchBytes(r, points.count, sizeof(bl_Vec3), &bytes));
    if (!l_typedSpan(points.data, points.count, alignof(bl_Vec3)))
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Invalid procedural point span");
    }
    for (size_t index = 0; index < points.count; ++index)
    {
        bl_Vec3 point = points.data[index];
        if (!isfinite(point.x) || !isfinite(point.y) || !isfinite(point.z))
        {
            return L_FAIL(r, BL_INVALID_ARGUMENT, "Nonfinite procedural point");
        }
    }
    return BL_OK;
}
