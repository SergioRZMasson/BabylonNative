/*
 * Hand port of original create-ribbon.ts, Babylon Lite 1.32.0.
 * Source 2e064d88ec7422af946f8ec7f089ac6519f99295, Apache-2.0 (LICENSE.txt).
 */
#include "GeometryDataInternal.h"

bl_Status l_ribbonBudget(bl_Runtime* r, size_t paths, size_t points, bool closeArray,
                         bool closePath, bool gpu, size_t extraScratch, size_t* outVertices,
                         size_t* outIndices)
{
    if (paths < 2 || (closePath && points == 0))
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Original Ribbon is missing paths or closing point");
    }
    size_t rows;
    size_t columns;
    size_t vertices;
    size_t indices = 0;
    size_t scratch;
    size_t distanceCount;
    if (!l_geometryAdd(paths, closeArray ? 1 : 0, &rows) ||
        !l_geometryAdd(points, closePath ? 1 : 0, &columns) || !l_size(rows, columns, &vertices))
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Ribbon count overflow");
    }
    if (columns == 1)
    {
        if (vertices < 3)
        {
            return L_FAIL(r, BL_INVALID_ARGUMENT,
                          "Original one-point Ribbon index exceeds vertices");
        }
        indices = 6;
    }
    else if (columns > 1)
    {
        if (!l_size(rows - 1, columns - 1, &indices) || !l_size(indices, 6, &indices))
        {
            return L_FAIL(r, BL_INVALID_ARGUMENT, "Ribbon index count overflow");
        }
    }
    if (!l_size(vertices, 2, &distanceCount) ||
        !l_geometryAdd(distanceCount, rows, &distanceCount) ||
        !l_geometryAdd(distanceCount, columns, &distanceCount) ||
        !l_size(distanceCount, sizeof(double), &scratch) ||
        !l_geometryAdd(scratch, extraScratch, &scratch))
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Ribbon distance scratch overflow");
    }
    L_TRY(l_geometryBudget(r, vertices, indices, gpu, scratch));
    *outVertices = vertices;
    *outIndices = indices;
    return BL_OK;
}

static void averageSeam(double* normals, size_t first, size_t last)
{
    normals[first] = (normals[first] + normals[last]) * 0.5;
    normals[first + 1] = (normals[first + 1] + normals[last + 1]) * 0.5;
    normals[first + 2] = (normals[first + 2] + normals[last + 2]) * 0.5;
    double length = sqrt(normals[first] * normals[first] + normals[first + 1] * normals[first + 1] +
                         normals[first + 2] * normals[first + 2]);
    if (length == 0)
    {
        length = 1;
    }
    normals[first] = normals[first] / length;
    normals[first + 1] = normals[first + 1] / length;
    normals[first + 2] = normals[first + 2] / length;
    normals[last] = normals[first];
    normals[last + 1] = normals[first + 1];
    normals[last + 2] = normals[first + 2];
}

bl_Status l_ribbonWork(bl_Runtime* r, bl_Vec3PathSpan paths, bool closeArray, bool closePath,
                       L_GeometryWork* work)
{
    size_t points = paths.data[0].count;
    size_t rows = paths.count + (closeArray ? 1 : 0);
    size_t columns = points + (closePath ? 1 : 0);
    size_t vertexBytes;
    size_t rowBytes;
    size_t columnBytes;
    L_TRY(l_geometryScratchBytes(r, work->vertices, sizeof(double), &vertexBytes));
    L_TRY(l_geometryScratchBytes(r, rows, sizeof(double), &rowBytes));
    L_TRY(l_geometryScratchBytes(r, columns, sizeof(double), &columnBytes));
    double* us = (double*)l_alloc(r, vertexBytes ? vertexBytes : sizeof(double));
    double* vs = (double*)l_alloc(r, vertexBytes ? vertexBytes : sizeof(double));
    double* uTotal = (double*)l_alloc(r, rowBytes);
    double* vTotal = (double*)l_alloc(r, columnBytes ? columnBytes : sizeof(double));
    if (!us || !vs || !uTotal || !vTotal)
    {
        l_free(r, us);
        l_free(r, vs);
        l_free(r, uTotal);
        l_free(r, vTotal);
        return L_FAIL(r, BL_OUT_OF_MEMORY, "Ribbon distance allocation failed");
    }
    for (size_t row = 0; row < rows; ++row)
    {
        bl_Vec3Span path = paths.data[row == paths.count ? 0 : row];
        uTotal[row] = 0;
        for (size_t point = 0; point < columns; ++point)
        {
            size_t sourcePoint = point == points ? 0 : point;
            bl_Vec3 position = path.data[sourcePoint];
            size_t vertex = row * columns + point;
            work->positions[vertex * 3] = position.x;
            work->positions[vertex * 3 + 1] = position.y;
            work->positions[vertex * 3 + 2] = position.z;
            double distance = 0;
            if (point > 0)
            {
                bl_Vec3 previous = path.data[point - 1];
                distance = point == points ? l_pathLength(l_pathSubtract(previous, position))
                                           : l_pathLength(l_pathSubtract(position, previous));
                distance = distance + uTotal[row];
            }
            us[vertex] = distance;
            uTotal[row] = distance;
        }
    }
    for (size_t point = 0; point < columns; ++point)
    {
        size_t sourcePoint = point == points ? 0 : point;
        vTotal[point] = 0;
        vs[point * rows] = 0;
        for (size_t row = 0; row < rows - 1; ++row)
        {
            bl_Vec3Span first = paths.data[row];
            bl_Vec3Span second = paths.data[row == paths.count - 1 ? 0 : row + 1];
            double distance =
                l_pathLength(l_pathSubtract(second.data[sourcePoint], first.data[sourcePoint]));
            distance = distance + vTotal[point];
            vs[point * rows + row + 1] = distance;
            vTotal[point] = distance;
        }
    }
    for (size_t row = 0; row < rows; ++row)
    {
        for (size_t point = 0; point < columns; ++point)
        {
            size_t vertex = row * columns + point;
            work->uvs[vertex * 2] = uTotal[row] != 0 ? us[vertex] / uTotal[row] : 0;
            work->uvs[vertex * 2 + 1] =
                vTotal[point] != 0 ? vs[point * rows + row] / vTotal[point] : 0;
        }
    }
    l_free(r, us);
    l_free(r, vs);
    l_free(r, uTotal);
    l_free(r, vTotal);
    size_t index = 0;
    if (columns)
    {
        size_t row = 0;
        size_t point = 0;
        size_t minimum = columns - 1;
        while (point <= minimum && row < rows - 1)
        {
            work->indices[index++] = (uint32_t)point;
            work->indices[index++] = (uint32_t)(point + columns);
            work->indices[index++] = (uint32_t)point + 1;
            work->indices[index++] = (uint32_t)(point + columns + 1);
            work->indices[index++] = (uint32_t)point + 1;
            work->indices[index++] = (uint32_t)(point + columns);
            ++point;
            if (point == minimum)
            {
                ++row;
                point = row * columns;
                minimum = columns - 1 + point;
            }
        }
    }
    L_TRY(l_geometryNormals(r, work));
    if (closePath)
    {
        for (size_t row = 0; row < paths.count; ++row)
        {
            size_t first = row * columns * 3;
            size_t last =
                row + 1 < paths.count ? ((row + 1) * columns - 1) * 3 : work->vertices * 3 - 3;
            averageSeam(work->normals, first, last);
        }
    }
    if (closeArray)
    {
        size_t first = 0;
        size_t last = paths.count * columns * 3;
        for (size_t point = 0; point < columns; ++point)
        {
            averageSeam(work->normals, first, last);
            first += 3;
            last += 3;
        }
    }
    return BL_OK;
}

bl_Status l_createRibbonData(bl_Runtime* r, const bl_RibbonOptions* o, bool gpu,
                             bl_GeometryData* out)
{
    L_TRY(l_check(r));
    if (!o || !out || !o->pathArray.count ||
        !l_typedSpan(o->pathArray.data, o->pathArray.count, alignof(bl_Vec3Span)))
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Ribbon requires a valid path table");
    }
    size_t tableBytes;
    L_TRY(l_geometryScratchBytes(r, o->pathArray.count, sizeof(bl_Vec3Span), &tableBytes));
    bl_Vec3PathSpan paths = o->pathArray;
    size_t points = paths.data[0].count;
    double offset = l_geometryOption(o->offset, floor((double)points / 2));
    if (!isfinite(offset))
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Nonfinite Ribbon offset");
    }
    bl_Vec3Span split[2];
    if (paths.count == 1)
    {
        if (!l_typedSpan(paths.data[0].data, points, alignof(bl_Vec3)))
        {
            return L_FAIL(r, BL_INVALID_ARGUMENT, "Invalid single Ribbon point span");
        }
        if (offset < 0)
        {
            return L_FAIL(r, BL_INVALID_ARGUMENT,
                          "Original Ribbon negative offset dereferences missing points");
        }
        double maximum = floor((double)points / 2);
        offset = offset > maximum ? maximum : floor(offset);
        if (points > UINT32_MAX)
        {
            return L_FAIL(r, BL_INVALID_ARGUMENT, "Ribbon point count overflow");
        }
        size_t shift = (size_t)offset;
        split[0] = {paths.data[0].data, points - shift};
        split[1] = {paths.data[0].data ? paths.data[0].data + shift : NULL, points - shift};
        paths = {split, 2};
        points -= shift;
    }
    size_t vertices;
    size_t indices;
    L_TRY(l_ribbonBudget(r, paths.count, points, o->closeArray, o->closePath, gpu, tableBytes,
                         &vertices, &indices));
    for (size_t row = 0; row < o->pathArray.count; ++row)
    {
        if (o->pathArray.data[row].count != o->pathArray.data[0].count)
        {
            return L_FAIL(r, BL_INVALID_ARGUMENT,
                          "Original jagged Ribbon has unrepresentable short UVs");
        }
        L_TRY(l_geometryPoints(r, o->pathArray.data[row]));
    }
    L_GeometryWork work;
    L_TRY(l_geometryWork(r, vertices, indices, &work));
    bl_Status status = l_ribbonWork(r, paths, o->closeArray, o->closePath, &work);
    if (status == BL_OK)
    {
        status = l_geometryFinish(r, &work, out);
    }
    l_geometryFree(r, &work);
    return status;
}

bl_Status bl_createRibbonData(bl_Runtime* r, const bl_RibbonOptions* o, bl_GeometryData* out)
{
    return l_createRibbonData(r, o, false, out);
}
