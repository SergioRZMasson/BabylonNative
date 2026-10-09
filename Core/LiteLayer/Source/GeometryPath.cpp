/*
 * Hand port of original path3d.ts and vector/Rodrigues bodies, Lite 1.32.0.
 * Source 2e064d88ec7422af946f8ec7f089ac6519f99295, Apache-2.0 (LICENSE.txt).
 */
#include "GeometryDataInternal.h"

bl_Vec3 l_pathSubtract(bl_Vec3 first, bl_Vec3 second)
{
    return {first.x - second.x, first.y - second.y, first.z - second.z};
}

bl_Vec3 l_pathCross(bl_Vec3 first, bl_Vec3 second)
{
    return {first.y * second.z - first.z * second.y, first.z * second.x - first.x * second.z,
            first.x * second.y - first.y * second.x};
}

double l_pathLength(bl_Vec3 vector)
{
    return sqrt(vector.x * vector.x + vector.y * vector.y + vector.z * vector.z);
}

bl_Vec3 l_pathNormalize(bl_Vec3 vector)
{
    double length = l_pathLength(vector);
    if (length <= 1e-10)
    {
        return {0, 0, 0};
    }
    double inverse = 1 / length;
    return {vector.x * inverse, vector.y * inverse, vector.z * inverse};
}

bl_Vec3 l_pathRotate(bl_Vec3 vector, bl_Vec3 axis, double angle)
{
    double cosine = cos(angle);
    double sine = sin(angle);
    double dot = axis.x * vector.x + axis.y * vector.y + axis.z * vector.z;
    double crossX = axis.y * vector.z - axis.z * vector.y;
    double crossY = axis.z * vector.x - axis.x * vector.z;
    double crossZ = axis.x * vector.y - axis.y * vector.x;
    return {vector.x * cosine + crossX * sine + axis.x * dot * (1 - cosine),
            vector.y * cosine + crossY * sine + axis.y * dot * (1 - cosine),
            vector.z * cosine + crossZ * sine + axis.z * dot * (1 - cosine)};
}

static bl_Vec3 firstNonNull(bl_Vec3Span curve, size_t index)
{
    size_t offset = 1;
    bl_Vec3 vector = l_pathSubtract(curve.data[index + offset], curve.data[index]);
    while (l_pathLength(vector) == 0 && index + offset + 1 < curve.count)
    {
        ++offset;
        vector = l_pathSubtract(curve.data[index + offset], curve.data[index]);
    }
    return vector;
}

static bl_Vec3 lastNonNull(bl_Vec3Span curve, size_t index)
{
    size_t offset = 1;
    bl_Vec3 vector = l_pathSubtract(curve.data[index], curve.data[index - offset]);
    while (l_pathLength(vector) == 0 && index > offset + 1)
    {
        ++offset;
        vector = l_pathSubtract(curve.data[index], curve.data[index - offset]);
    }
    return vector;
}

static bl_Vec3 initialNormal(bl_Vec3 tangent)
{
    double length = l_pathLength(tangent);
    if (length == 0)
    {
        length = 1;
    }
    bl_Vec3 point;
    if (!(fabs(fabs(tangent.y) / length - 1) <= 0.001))
    {
        point = {0, -1, 0};
    }
    else if (!(fabs(fabs(tangent.x) / length - 1) <= 0.001))
    {
        point = {1, 0, 0};
    }
    else if (!(fabs(fabs(tangent.z) / length - 1) <= 0.001))
    {
        point = {0, 0, 1};
    }
    else
    {
        point = {0, 0, 0};
    }
    return l_pathNormalize(l_pathCross(tangent, point));
}

bl_Status l_computePath3D(bl_Runtime* r, bl_Vec3Span curve, L_Path3D* out)
{
    if (!curve.count)
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Original sweep frame is missing for this path");
    }
    size_t vectorBytes;
    size_t distanceBytes;
    L_TRY(l_geometryScratchBytes(r, curve.count, sizeof(bl_Vec3), &vectorBytes));
    L_TRY(l_geometryScratchBytes(r, curve.count, sizeof(double), &distanceBytes));
    L_Path3D path = {};
    path.count = curve.count;
    path.tangents = (bl_Vec3*)l_alloc(r, vectorBytes);
    path.normals = (bl_Vec3*)l_alloc(r, vectorBytes);
    path.binormals = (bl_Vec3*)l_alloc(r, vectorBytes);
    path.distances = (double*)l_alloc(r, distanceBytes);
    if (!path.tangents || !path.normals || !path.binormals || !path.distances)
    {
        l_freePath3D(r, &path);
        return L_FAIL(r, BL_OUT_OF_MEMORY, "Path3D allocation failed");
    }
    if (curve.count < 2)
    {
        *out = path;
        return BL_OK;
    }
    path.tangents[0] = l_pathNormalize(firstNonNull(curve, 0));
    path.tangents[curve.count - 1] =
        l_pathNormalize(l_pathSubtract(curve.data[curve.count - 1], curve.data[curve.count - 2]));
    path.normals[0] = l_pathNormalize(initialNormal(path.tangents[0]));
    path.binormals[0] = l_pathNormalize(l_pathCross(path.tangents[0], path.normals[0]));
    path.distances[0] = 0;
    for (size_t index = 1; index < curve.count; ++index)
    {
        bl_Vec3 previous = lastNonNull(curve, index);
        if (index < curve.count - 1)
        {
            bl_Vec3 current = firstNonNull(curve, index);
            bl_Vec3 sum = {previous.x + current.x, previous.y + current.y, previous.z + current.z};
            path.tangents[index] = l_pathNormalize(sum);
        }
        path.distances[index] =
            path.distances[index - 1] +
            l_pathLength(l_pathSubtract(curve.data[index], curve.data[index - 1]));
        bl_Vec3 normal = l_pathCross(path.binormals[index - 1], path.tangents[index]);
        if (l_pathLength(normal) == 0)
        {
            normal = path.normals[index - 1];
        }
        else
        {
            normal = l_pathNormalize(normal);
        }
        path.normals[index] = normal;
        path.binormals[index] = l_pathNormalize(l_pathCross(path.tangents[index], normal));
    }
    *out = path;
    return BL_OK;
}

void l_freePath3D(bl_Runtime* r, L_Path3D* path)
{
    l_free(r, path->tangents);
    l_free(r, path->normals);
    l_free(r, path->binormals);
    l_free(r, path->distances);
    *path = {};
}
