/*
 * Hand port of original create-plane.ts/create-disc.ts, Babylon Lite 1.32.0.
 * Source 2e064d88ec7422af946f8ec7f089ac6519f99295, Apache-2.0 (LICENSE.txt).
 */
#include "GeometryDataInternal.h"

bl_Status l_createPlaneData(bl_Runtime* r, const bl_PlaneOptions* options, bool gpu,
                            bl_GeometryData* out)
{
    L_TRY(l_check(r));
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    bl_PlaneOptions defaults = {};
    const bl_PlaneOptions* o = options ? options : &defaults;
    double size = l_geometryOption(o->size, 1);
    double width = l_geometryOption(o->width, size);
    double height = l_geometryOption(o->height, size);
    if (!isfinite(width) || !isfinite(height) || (o->size.present && !isfinite(size)))
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Nonfinite Plane options");
    }
    L_TRY(l_geometryBudget(r, 4, 6, gpu));
    L_GeometryWork work;
    L_TRY(l_geometryWork(r, 4, 6, &work));
    double halfWidth = width / 2;
    double halfHeight = height / 2;
    const double positions[12] = {-halfWidth, -halfHeight, 0, halfWidth,  -halfHeight, 0,
                                  halfWidth,  halfHeight,  0, -halfWidth, halfHeight,  0};
    const double normals[12] = {0, 0, -1, 0, 0, -1, 0, 0, -1, 0, 0, -1};
    const double uvs[8] = {0, 0, 1, 0, 1, 1, 0, 1};
    const uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
    memcpy(work.positions, positions, sizeof(positions));
    memcpy(work.normals, normals, sizeof(normals));
    memcpy(work.uvs, uvs, sizeof(uvs));
    memcpy(work.indices, indices, sizeof(indices));
    bl_Status status = l_geometryFinish(r, &work, out);
    l_geometryFree(r, &work);
    return status;
}

bl_Status bl_createPlaneData(bl_Runtime* r, const bl_PlaneOptions* o, bl_GeometryData* out)
{
    return l_createPlaneData(r, o, false, out);
}

bl_Status l_createDiscData(bl_Runtime* r, const bl_DiscOptions* options, bool gpu,
                           bl_GeometryData* out)
{
    L_TRY(l_check(r));
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    bl_DiscOptions defaults = {};
    const bl_DiscOptions* o = options ? options : &defaults;
    double radius = l_geometryOption(o->radius, 0.5);
    double tessellation = l_geometryOption(o->tessellation, 64);
    double arc = l_geometryOption(o->arc, 1);
    if (!isfinite(radius) || !isfinite(tessellation) || !isfinite(arc))
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Nonfinite Disc options");
    }
    if (arc != 0 && (arc <= 0 || arc > 1))
    {
        arc = 1;
    }
    double iterations = tessellation > 0 ? ceil(tessellation) : 0;
    if (iterations > UINT32_MAX - 2 || (arc == 1 && iterations == 0))
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Original Disc has missing seam or excessive count");
    }
    size_t points = (size_t)iterations;
    size_t vertices = points + 1 + (arc == 1 ? 1 : 0);
    size_t indices = vertices > 2 ? (vertices - 2) * 3 : 0;
    L_TRY(l_geometryBudget(r, vertices, indices, gpu));
    L_GeometryWork work;
    L_TRY(l_geometryWork(r, vertices, indices, &work));
    work.positions[0] = 0;
    work.positions[1] = 0;
    work.positions[2] = 0;
    work.uvs[0] = 0.5;
    work.uvs[1] = 0.5;
    double theta = 3.141592653589793 * 2 * arc;
    double step = arc == 1 ? theta / tessellation : theta / (tessellation - 1);
    double angle = 0;
    for (size_t point = 0; point < points; ++point)
    {
        double x = cos(angle);
        double y = sin(angle);
        work.positions[(point + 1) * 3] = radius * x;
        work.positions[(point + 1) * 3 + 1] = radius * y;
        work.positions[(point + 1) * 3 + 2] = 0;
        work.uvs[(point + 1) * 2] = (x + 1) / 2;
        work.uvs[(point + 1) * 2 + 1] = (1 - y) / 2;
        angle += step;
    }
    if (arc == 1)
    {
        memcpy(work.positions + (vertices - 1) * 3, work.positions + 3, 3 * sizeof(double));
        memcpy(work.uvs + (vertices - 1) * 2, work.uvs + 2, 2 * sizeof(double));
    }
    size_t index = 0;
    for (size_t vertex = 1; vertex + 1 < vertices; ++vertex)
    {
        work.indices[index++] = (uint32_t)vertex + 1;
        work.indices[index++] = 0;
        work.indices[index++] = (uint32_t)vertex;
    }
    for (size_t vertex = 0; vertex < vertices; ++vertex)
    {
        work.normals[vertex * 3] = 0;
        work.normals[vertex * 3 + 1] = 0;
        work.normals[vertex * 3 + 2] = -1;
    }
    bl_Status status = l_geometryFinish(r, &work, out);
    l_geometryFree(r, &work);
    return status;
}

bl_Status bl_createDiscData(bl_Runtime* r, const bl_DiscOptions* o, bl_GeometryData* out)
{
    return l_createDiscData(r, o, false, out);
}
