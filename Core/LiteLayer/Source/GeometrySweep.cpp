/*
 * Hand port of original create-tube.ts/create-extrude.ts, Lite 1.32.0.
 * Source 2e064d88ec7422af946f8ec7f089ac6519f99295, Apache-2.0 (LICENSE.txt).
 */
#include "GeometryDataInternal.h"

static bl_Status sweepBudget(bl_Runtime* r, size_t rows, size_t points, size_t curvePoints,
                             bool tube, bool gpu, size_t* outVertices, size_t* outIndices)
{
    size_t grid;
    size_t gridBytes;
    size_t rowBytes;
    size_t frameBytes;
    size_t scratch;
    if (!l_size(rows, points, &grid) || !l_size(grid, sizeof(bl_Vec3), &gridBytes) ||
        !l_size(rows, sizeof(bl_Vec3Span), &rowBytes) ||
        !l_size(curvePoints, 3 * sizeof(bl_Vec3) + sizeof(double), &frameBytes) ||
        !l_geometryAdd(gridBytes, rowBytes, &scratch) ||
        !l_geometryAdd(scratch, frameBytes, &scratch) || !l_allocationFits(gridBytes) ||
        !l_allocationFits(rowBytes) || !l_allocationFits(frameBytes))
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Sweep frame or grid byte overflow");
    }
    return l_ribbonBudget(r, rows, points, false, tube, gpu, scratch, outVertices, outIndices);
}

static bl_Status sweepRows(bl_Runtime* r, size_t rows, size_t points, bl_Vec3** outPoints,
                           bl_Vec3Span** outRows)
{
    size_t pointCount;
    size_t pointBytes;
    size_t rowBytes;
    if (!l_size(rows, points, &pointCount))
    {
        return BL_INVALID_ARGUMENT;
    }
    L_TRY(l_geometryScratchBytes(r, pointCount, sizeof(bl_Vec3), &pointBytes));
    L_TRY(l_geometryScratchBytes(r, rows, sizeof(bl_Vec3Span), &rowBytes));
    bl_Vec3* grid = (bl_Vec3*)l_alloc(r, pointBytes ? pointBytes : sizeof(bl_Vec3));
    bl_Vec3Span* spans = (bl_Vec3Span*)l_alloc(r, rowBytes);
    if (!grid || !spans)
    {
        l_free(r, grid);
        l_free(r, spans);
        return L_FAIL(r, BL_OUT_OF_MEMORY, "Sweep grid allocation failed");
    }
    for (size_t row = 0; row < rows; ++row)
    {
        spans[row] = {grid + row * points, points};
    }
    *outPoints = grid;
    *outRows = spans;
    return BL_OK;
}

static bl_Status finishSweep(bl_Runtime* r, bl_Vec3PathSpan rows, bool tube, size_t vertices,
                             size_t indices, bl_GeometryData* out)
{
    L_GeometryWork work;
    L_TRY(l_geometryWork(r, vertices, indices, &work));
    bl_Status status = l_ribbonWork(r, rows, false, tube, &work);
    if (status == BL_OK)
    {
        status = l_geometryFinish(r, &work, out);
    }
    l_geometryFree(r, &work);
    return status;
}

static bool validCap(bl_GeometryCap cap)
{
    return cap == BL_CAP_NONE || cap == BL_CAP_START || cap == BL_CAP_END || cap == BL_CAP_ALL;
}

bl_Status l_createTubeData(bl_Runtime* r, const bl_TubeOptions* o, bool gpu, bl_GeometryData* out)
{
    L_TRY(l_check(r));
    if (!o || !out || !validCap(o->cap) || o->path.count < 2)
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Tube requires a valid original path and cap");
    }
    double radius = l_geometryOption(o->radius, 1);
    double tessellation = l_geometryOption(o->tessellation, 64);
    double arc = l_geometryOption(o->arc, 1);
    if (!isfinite(radius) || !isfinite(tessellation) || !isfinite(arc))
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Nonfinite Tube options");
    }
    tessellation = l_geometryInt32(tessellation);
    if (tessellation <= 0)
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Original Tube has an empty closing circle");
    }
    if (arc != 0 && (arc <= 0 || arc > 1))
    {
        arc = 1;
    }
    bool startCap = o->cap == BL_CAP_START || o->cap == BL_CAP_ALL;
    bool endCap = o->cap == BL_CAP_END || o->cap == BL_CAP_ALL;
    size_t points = (size_t)tessellation;
    size_t rowCount;
    if (!l_geometryAdd(o->path.count, (startCap ? 2 : 0) + (endCap ? 2 : 0), &rowCount))
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Tube row count overflow");
    }
    size_t vertices;
    size_t indices;
    L_TRY(sweepBudget(r, rowCount, points, o->path.count, true, gpu, &vertices, &indices));
    L_TRY(l_geometryPoints(r, o->path));
    L_Path3D frame;
    L_TRY(l_computePath3D(r, o->path, &frame));
    bl_Vec3* grid;
    bl_Vec3Span* rows;
    bl_Status status = sweepRows(r, rowCount, points, &grid, &rows);
    if (status != BL_OK)
    {
        l_freePath3D(r, &frame);
        return status;
    }
    size_t index = startCap ? 2 : 0;
    double step = ((3.141592653589793 * 2) / tessellation) * arc;
    for (size_t point = 0; point < o->path.count; ++point)
    {
        for (size_t circle = 0; circle < points; ++circle)
        {
            bl_Vec3 rotated =
                l_pathRotate(frame.normals[point], frame.tangents[point], step * (double)circle);
            grid[index * points + circle] = {rotated.x * radius + o->path.data[point].x,
                                             rotated.y * radius + o->path.data[point].y,
                                             rotated.z * radius + o->path.data[point].z};
        }
        ++index;
    }
    if (startCap)
    {
        for (size_t point = 0; point < points; ++point)
        {
            grid[point] = o->path.data[0];
        }
        memcpy(grid + points, grid + 2 * points, points * sizeof(bl_Vec3));
    }
    if (endCap)
    {
        memcpy(grid + index * points, grid + (index - 1) * points, points * sizeof(bl_Vec3));
        for (size_t point = 0; point < points; ++point)
        {
            grid[(index + 1) * points + point] = o->path.data[o->path.count - 1];
        }
    }
    l_freePath3D(r, &frame);
    status = finishSweep(r, {rows, rowCount}, true, vertices, indices, out);
    l_free(r, grid);
    l_free(r, rows);
    return status;
}

bl_Status bl_createTubeData(bl_Runtime* r, const bl_TubeOptions* o, bl_GeometryData* out)
{
    return l_createTubeData(r, o, false, out);
}

static void barycenterRow(bl_Vec3* destination, const bl_Vec3* source, size_t points)
{
    if (!points)
    {
        return;
    }
    bl_Vec3 center = {0, 0, 0};
    for (size_t point = 0; point < points; ++point)
    {
        center.x += source[point].x;
        center.y += source[point].y;
        center.z += source[point].z;
    }
    double inverse = 1 / (double)points;
    center.x *= inverse;
    center.y *= inverse;
    center.z *= inverse;
    for (size_t point = 0; point < points; ++point)
    {
        destination[point] = center;
    }
}

bl_Status l_createExtrudeShapeData(bl_Runtime* r, const bl_ExtrudeShapeOptions* o, bool gpu,
                                   bl_GeometryData* out)
{
    L_TRY(l_check(r));
    if (!o || !out || !validCap(o->cap) || !o->path.count || (o->shape.count && o->path.count < 2))
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT,
                      "Extrusion requires a representable original path and cap");
    }
    double scale = l_geometryOption(o->scale, 1);
    double rotation = l_geometryOption(o->rotation, 0);
    if (!isfinite(scale) || !isfinite(rotation))
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Nonfinite ExtrudeShape options");
    }
    bool startCap = o->cap == BL_CAP_START || o->cap == BL_CAP_ALL;
    bool endCap = o->cap == BL_CAP_END || o->cap == BL_CAP_ALL;
    size_t rowCount;
    if (!l_geometryAdd(o->path.count, (startCap ? 2 : 0) + (endCap ? 2 : 0), &rowCount))
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Extrusion row count overflow");
    }
    if (rowCount == 1)
    {
        rowCount = 2;
    }
    size_t vertices;
    size_t indices;
    L_TRY(sweepBudget(r, rowCount, o->shape.count, o->path.count, false, gpu, &vertices, &indices));
    L_TRY(l_geometryPoints(r, o->path));
    L_TRY(l_geometryPoints(r, o->shape));
    L_Path3D frame;
    L_TRY(l_computePath3D(r, o->path, &frame));
    bl_Vec3* grid;
    bl_Vec3Span* rows;
    bl_Status status = sweepRows(r, rowCount, o->shape.count, &grid, &rows);
    if (status != BL_OK)
    {
        l_freePath3D(r, &frame);
        return status;
    }
    size_t points = o->shape.count;
    size_t index = startCap ? 2 : 0;
    double angle = 0;
    for (size_t point = 0; point < o->path.count; ++point)
    {
        for (size_t shape = 0; shape < points; ++shape)
        {
            bl_Vec3 t = frame.tangents[point];
            bl_Vec3 n = frame.normals[point];
            bl_Vec3 b = frame.binormals[point];
            bl_Vec3 sp = o->shape.data[shape];
            bl_Vec3 planed = {t.x * sp.z + n.x * sp.x + b.x * sp.y,
                              t.y * sp.z + n.y * sp.x + b.y * sp.y,
                              t.z * sp.z + n.z * sp.x + b.z * sp.y};
            bl_Vec3 rotated = l_pathRotate(planed, t, angle);
            grid[index * points + shape] = {rotated.x * scale + o->path.data[point].x,
                                            rotated.y * scale + o->path.data[point].y,
                                            rotated.z * scale + o->path.data[point].z};
        }
        angle += rotation;
        ++index;
    }
    if (startCap)
    {
        barycenterRow(grid, grid + 2 * points, points);
        memcpy(grid + points, grid + 2 * points, points * sizeof(bl_Vec3));
    }
    if (endCap)
    {
        memcpy(grid + index * points, grid + (index - 1) * points, points * sizeof(bl_Vec3));
        barycenterRow(grid + (index + 1) * points, grid + (index - 1) * points, points);
    }
    l_freePath3D(r, &frame);
    status = finishSweep(r, {rows, rowCount}, false, vertices, indices, out);
    l_free(r, grid);
    l_free(r, rows);
    return status;
}

bl_Status bl_createExtrudeShapeData(bl_Runtime* r, const bl_ExtrudeShapeOptions* o,
                                    bl_GeometryData* out)
{
    return l_createExtrudeShapeData(r, o, false, out);
}
