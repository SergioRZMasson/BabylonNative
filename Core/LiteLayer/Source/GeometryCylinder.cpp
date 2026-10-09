/*
 * Hand port of original create-cylinder.ts, Babylon Lite 1.32.0.
 * Source 2e064d88ec7422af946f8ec7f089ac6519f99295, Apache-2.0 (LICENSE.txt).
 */
#include "GeometryDataInternal.h"

static void cylinderCap(L_GeometryWork* work, double diameter, double height, size_t tessellation,
                        bool top, size_t* vertex, size_t* index)
{
    double radius = diameter / 2;
    size_t base = *vertex;
    double offset = top ? height / 2 : -height / 2;
    work->positions[base * 3] = 0;
    work->positions[base * 3 + 1] = offset;
    work->positions[base * 3 + 2] = 0;
    work->normals[base * 3] = 0;
    work->normals[base * 3 + 1] = top ? 1 : -1;
    work->normals[base * 3 + 2] = 0;
    work->uvs[base * 2] = 0.5;
    work->uvs[base * 2 + 1] = 0.5;
    ++*vertex;
    for (size_t point = 0; point <= tessellation; ++point)
    {
        double angle = (3.141592653589793 * 2 * (double)point * 1) / (double)tessellation;
        double cosine = cos(-angle);
        double sine = sin(-angle);
        size_t current = (*vertex)++;
        work->positions[current * 3] = cosine * radius;
        work->positions[current * 3 + 1] = offset;
        work->positions[current * 3 + 2] = sine * radius;
        work->normals[current * 3] = 0;
        work->normals[current * 3 + 1] = top ? 1 : -1;
        work->normals[current * 3 + 2] = 0;
        work->uvs[current * 2] = cosine * 0.5 + 0.5;
        work->uvs[current * 2 + 1] = sine * 0.5 + 0.5;
    }
    for (size_t point = 0; point < tessellation; ++point)
    {
        work->indices[(*index)++] = (uint32_t)base;
        work->indices[(*index)++] = (uint32_t)(base + point + (top ? 2 : 1));
        work->indices[(*index)++] = (uint32_t)(base + point + (top ? 1 : 2));
    }
}

bl_Status l_createCylinderData(bl_Runtime* r, const bl_CylinderOptions* options, bool gpu,
                               bl_GeometryData* out)
{
    L_TRY(l_check(r));
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    bl_CylinderOptions defaults = {};
    const bl_CylinderOptions* o = options ? options : &defaults;
    double height = l_geometryOption(o->height, 2);
    double diameter = l_geometryOption(o->diameter, 1);
    double top = l_geometryOption(o->diameterTop, diameter);
    double bottom = l_geometryOption(o->diameterBottom, diameter);
    double tess = l_geometryOption(o->tessellation, 24);
    double subdiv = l_geometryOption(o->subdivisions, 1);
    if (!isfinite(height) || height == 0 || !isfinite(top) || !isfinite(bottom) ||
        !isfinite(tess) || !isfinite(subdiv) || (o->diameter.present && !isfinite(diameter)))
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Invalid original finite Cylinder options");
    }
    if (top == 0)
    {
        top = 0.00001;
    }
    if (bottom == 0)
    {
        bottom = 0.00001;
    }
    tess = l_geometryInt32(tess);
    subdiv = l_geometryInt32(subdiv);
    if (tess < 3)
    {
        tess = 3;
    }
    if (subdiv < 1)
    {
        subdiv = 1;
    }
    size_t tessellation = (size_t)tess;
    size_t subdivisions = (size_t)subdiv;
    size_t vertices;
    size_t caps;
    size_t indices;
    if (!l_size(subdivisions + 1, tessellation + 1, &vertices) ||
        !l_size(tessellation + 2, 2, &caps) || !l_geometryAdd(vertices, caps, &vertices) ||
        !l_size(tessellation, subdivisions + 1, &indices) || !l_size(indices, 6, &indices))
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Cylinder count overflow");
    }
    L_TRY(l_geometryBudget(r, vertices, indices, gpu));
    L_GeometryWork work;
    L_TRY(l_geometryWork(r, vertices, indices, &work));
    double angleStep = (3.141592653589793 * 2 * 1) / tess;
    double tangent = (bottom - top) / 2 / height;
    size_t vertex = 0;
    for (size_t row = 0; row <= subdivisions; ++row)
    {
        double h = (double)row / subdiv;
        double radius = (h * (top - bottom) + bottom) / 2;
        for (size_t point = 0; point <= tessellation; ++point)
        {
            double angle = (double)point * angleStep;
            double x = cos(-angle) * radius;
            double y = -height / 2 + h * height;
            double z = sin(-angle) * radius;
            double nx;
            double ny;
            double nz;
            if (o->diameterTop.present && o->diameterTop.value == 0 && row == subdivisions)
            {
                size_t base = (vertex - (tessellation + 1)) * 3;
                nx = work.normals[base];
                ny = work.normals[base + 1];
                nz = work.normals[base + 2];
            }
            else
            {
                nx = x;
                nz = z;
                ny = sqrt(nx * nx + nz * nz) * tangent;
                double inverseLength = 1 / sqrt(nx * nx + ny * ny + nz * nz);
                nx *= inverseLength;
                ny *= inverseLength;
                nz *= inverseLength;
            }
            work.positions[vertex * 3] = x;
            work.positions[vertex * 3 + 1] = y;
            work.positions[vertex * 3 + 2] = z;
            work.normals[vertex * 3] = nx;
            work.normals[vertex * 3 + 1] = ny;
            work.normals[vertex * 3 + 2] = nz;
            work.uvs[vertex * 2] = (double)point / tess;
            work.uvs[vertex * 2 + 1] = h;
            ++vertex;
        }
    }
    size_t index = 0;
    for (size_t row = 0; row < subdivisions; ++row)
    {
        for (size_t point = 0; point < tessellation; ++point)
        {
            uint32_t i0 = (uint32_t)(row * (tessellation + 1) + point);
            uint32_t i1 = (uint32_t)((row + 1) * (tessellation + 1) + point);
            uint32_t i2 = i0 + 1;
            uint32_t i3 = i1 + 1;
            work.indices[index++] = i0;
            work.indices[index++] = i1;
            work.indices[index++] = i2;
            work.indices[index++] = i3;
            work.indices[index++] = i2;
            work.indices[index++] = i1;
        }
    }
    cylinderCap(&work, bottom, height, tessellation, false, &vertex, &index);
    cylinderCap(&work, top, height, tessellation, true, &vertex, &index);
    bl_Status status = l_geometryFinish(r, &work, out);
    l_geometryFree(r, &work);
    return status;
}

bl_Status bl_createCylinderData(bl_Runtime* r, const bl_CylinderOptions* o, bl_GeometryData* out)
{
    return l_createCylinderData(r, o, false, out);
}
