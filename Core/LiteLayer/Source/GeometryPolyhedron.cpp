/*
 * Hand port of original create-polyhedron.ts, Babylon Lite 1.32.0.
 * Source 2e064d88ec7422af946f8ec7f089ac6519f99295, Apache-2.0 (LICENSE.txt).
 */
#include "GeometryDataInternal.h"
#include "GeometryPolyhedronData.h"

bl_Status l_createPolyhedronData(bl_Runtime* r, const bl_PolyhedronOptions* options, bool gpu,
                                 bl_GeometryData* out)
{
    L_TRY(l_check(r));
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    bl_PolyhedronOptions defaults = {};
    const bl_PolyhedronOptions* o = options ? options : &defaults;
    double type = l_geometryOption(o->type, 0);
    double size = l_geometryOption(o->size, 1);
    double sizeX = l_geometryOption(o->sizeX, size);
    double sizeY = l_geometryOption(o->sizeY, size);
    double sizeZ = l_geometryOption(o->sizeZ, size);
    if (!isfinite(type) || !isfinite(sizeX) || !isfinite(sizeY) || !isfinite(sizeZ) ||
        (o->size.present && !isfinite(size)) ||
        (o->flat != BL_BOOL_DEFAULT && o->flat != BL_BOOL_FALSE && o->flat != BL_BOOL_TRUE))
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Nonfinite Polyhedron options");
    }
    if (type < 0 || type >= 15)
    {
        type = 0;
    }
    if (floor(type) != type)
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT,
                      "Original Polyhedron preset is missing for fractional type");
    }
    const L_PolyhedronPreset* preset = &polyhedra[(size_t)type];
    bool flat = o->flat != BL_BOOL_FALSE;
    size_t vertices = flat ? 0 : preset->vertexCount;
    size_t indices = 0;
    for (size_t face = 0; face < preset->faceCount; ++face)
    {
        if (flat)
        {
            vertices += preset->faces[face].count;
        }
        indices += (preset->faces[face].count - 2) * 3;
    }
    L_TRY(l_geometryBudget(r, vertices, indices, gpu));
    L_GeometryWork work;
    L_TRY(l_geometryWork(r, vertices, indices, &work));
    size_t vertex = 0;
    if (!flat)
    {
        for (; vertex < vertices; ++vertex)
        {
            bl_Vec3 position = preset->vertices[vertex];
            work.positions[vertex * 3] = position.x * sizeX;
            work.positions[vertex * 3 + 1] = position.y * sizeY;
            work.positions[vertex * 3 + 2] = position.z * sizeZ;
            work.uvs[vertex * 2] = 0;
            work.uvs[vertex * 2 + 1] = 0;
        }
    }
    size_t index = 0;
    for (size_t face = 0; face < preset->faceCount; ++face)
    {
        const L_PolyhedronFace* current = &preset->faces[face];
        size_t base = vertex;
        if (flat)
        {
            double angle = (2 * 3.141592653589793) / (double)current->count;
            double x = 0.5 * tan(angle / 2);
            double y = 0.5;
            for (size_t point = 0; point < current->count; ++point)
            {
                bl_Vec3 position = preset->vertices[current->vertices[point]];
                work.positions[vertex * 3] = position.x * sizeX;
                work.positions[vertex * 3 + 1] = position.y * sizeY;
                work.positions[vertex * 3 + 2] = position.z * sizeZ;
                work.uvs[vertex * 2] = 0.5 + x;
                work.uvs[vertex * 2 + 1] = y - 0.5 + 0.5;
                double temporary = x * cos(angle) - y * sin(angle);
                y = x * sin(angle) + y * cos(angle);
                x = temporary;
                ++vertex;
            }
        }
        for (size_t point = 0; point < current->count - 2; ++point)
        {
            work.indices[index++] = flat ? (uint32_t)base : current->vertices[0];
            work.indices[index++] =
                flat ? (uint32_t)(base + point + 2) : current->vertices[point + 2];
            work.indices[index++] =
                flat ? (uint32_t)(base + point + 1) : current->vertices[point + 1];
        }
    }
    bl_Status status = l_geometryNormals(r, &work);
    if (status == BL_OK)
    {
        status = l_geometryFinish(r, &work, out);
    }
    l_geometryFree(r, &work);
    return status;
}

bl_Status bl_createPolyhedronData(bl_Runtime* r, const bl_PolyhedronOptions* o,
                                  bl_GeometryData* out)
{
    return l_createPolyhedronData(r, o, false, out);
}
