/*
 * Babylon Lite 1.32.0 mesh/create-torus.ts and mesh/create-ground.ts.
 * Source 2e064d88ec7422af946f8ec7f089ac6519f99295, Apache-2.0.
 */
#include "GeometryDataInternal.h"

bl_Status bl_createTorusData(bl_Runtime* r, const bl_TorusOptions* o, bl_GeometryData* out)
{
    L_TRY(l_check(r));
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    double diameter = o ? l_geometryOption(o->diameter, 1) : 1;
    double thickness = o ? l_geometryOption(o->thickness, .5) : .5;
    double tessellation = o ? l_geometryOption(o->tessellation, 16) : 16;
    if (!isfinite(diameter) || !isfinite(thickness) || !isfinite(tessellation) ||
        tessellation < 1 || floor(tessellation) != tessellation || tessellation >= UINT32_MAX)
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Invalid torus options");
    }
    size_t stride = (size_t)tessellation + 1;
    size_t vertices;
    size_t indices;
    if (!l_size(stride, stride, &vertices) || !l_size(vertices, 6, &indices))
    {
        return BL_INVALID_ARGUMENT;
    }
    L_TRY(l_geometryBudget(r, vertices, indices, true));
    L_GeometryWork work;
    L_TRY(l_geometryWork(r, vertices, indices, &work));
    size_t vertex = 0;
    size_t index = 0;
    double major = diameter / 2;
    double minor = thickness / 2;
    double twoPi = 3.141592653589793 * 2;
    for (size_t i = 0; i < stride; ++i)
    {
        double outer = (i * twoPi) / tessellation - 3.141592653589793 / 2;
        double co = cos(outer);
        double si = sin(outer);
        for (size_t j = 0; j < stride; ++j, ++vertex)
        {
            double inner = (j * twoPi) / tessellation + 3.141592653589793;
            double dx = cos(inner);
            double dy = sin(inner);
            double px = dx * minor;
            work.positions[vertex * 3] = (px + major) * co;
            work.positions[vertex * 3 + 1] = dy * minor;
            work.positions[vertex * 3 + 2] = -(px + major) * si;
            work.normals[vertex * 3] = dx * co;
            work.normals[vertex * 3 + 1] = dy;
            work.normals[vertex * 3 + 2] = -dx * si;
            work.uvs[vertex * 2] = i / tessellation;
            work.uvs[vertex * 2 + 1] = 1 - j / tessellation;
            size_t nextI = (i + 1) % stride;
            size_t nextJ = (j + 1) % stride;
            work.indices[index++] = (uint32_t)(i * stride + j);
            work.indices[index++] = (uint32_t)(i * stride + nextJ);
            work.indices[index++] = (uint32_t)(nextI * stride + j);
            work.indices[index++] = (uint32_t)(i * stride + nextJ);
            work.indices[index++] = (uint32_t)(nextI * stride + nextJ);
            work.indices[index++] = (uint32_t)(nextI * stride + j);
        }
    }
    bl_Status status = l_geometryFinish(r, &work, out);
    l_geometryFree(r, &work);
    return status;
}

static void heightNormals(bl_GeometryData* data)
{
    memset(data->normals, 0, data->vertexCount * 3 * sizeof(float));
    for (size_t i = 0; i < data->indexCount; i += 3)
    {
        size_t a = (size_t)data->indices[i] * 3;
        size_t b = (size_t)data->indices[i + 1] * 3;
        size_t c = (size_t)data->indices[i + 2] * 3;
        double x1 = (double)data->positions[b] - data->positions[a];
        double y1 = (double)data->positions[b + 1] - data->positions[a + 1];
        double z1 = (double)data->positions[b + 2] - data->positions[a + 2];
        double x2 = (double)data->positions[c] - data->positions[a];
        double y2 = (double)data->positions[c + 1] - data->positions[a + 1];
        double z2 = (double)data->positions[c + 2] - data->positions[a + 2];
        double normal[3] = {-(y1 * z2 - z1 * y2), -(z1 * x2 - x1 * z2), -(x1 * y2 - y1 * x2)};
        double length = sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
        if (length == 0)
        {
            length = 1;
        }
        for (size_t axis = 0; axis < 3; ++axis)
        {
            double value = normal[axis] / length;
            data->normals[a + axis] = (float)(data->normals[a + axis] + value);
            data->normals[b + axis] = (float)(data->normals[b + axis] + value);
            data->normals[c + axis] = (float)(data->normals[c + axis] + value);
        }
    }
    for (size_t i = 0; i < data->vertexCount * 3; i += 3)
    {
        double x = data->normals[i];
        double y = data->normals[i + 1];
        double z = data->normals[i + 2];
        double length = sqrt(x * x + y * y + z * z);
        if (length == 0)
        {
            length = 1;
        }
        data->normals[i] = (float)(x / length);
        data->normals[i + 1] = (float)(y / length);
        data->normals[i + 2] = (float)(z / length);
    }
}

bl_Status bl_createGroundFromHeightMapData(bl_Runtime* r, bl_String url,
                                           const bl_HeightMapGroundOptions* o, bl_GeometryData* out)
{
    L_TRY(l_check(r));
    double low = o ? l_geometryOption(o->minHeight, 0) : 0;
    double high = o ? l_geometryOption(o->maxHeight, 1) : 1;
    if (!out || !l_string(url) || !url.length || !isfinite(low) || !isfinite(high) ||
        !isfinite((float)low) || !isfinite((float)high))
    {
        return BL_INVALID_ARGUMENT;
    }
    if (r->frameDepth || r->dispatchDepth)
    {
        return BL_BUSY;
    }
    if (!r->hasIO || !r->io.readFile || !r->io.releaseBuffer || !r->io.decodeImage ||
        !r->io.releaseImage)
    {
        return L_FAIL(r, BL_UNSUPPORTED, "Heightmap requires the host image service");
    }
    bl_GeometryData data;
    L_TRY(bl_createFlatGroundData(r, o ? &o->ground : NULL, &data));
    bl_HostBuffer buffer = {};
    bl_HostImage image = {};
    bl_Status status;
    L_CALL(r, status, r->io.readFile(r->io.userData, url, &buffer));
    bool decoded = false;
    if (status == BL_OK)
    {
        if (!l_span(buffer.data, buffer.byteCount) || !buffer.byteCount)
        {
            status = BL_HOST_ERROR;
        }
        else
        {
            decoded = true;
            L_CALL(r, status,
                   r->io.decodeImage(r->io.userData, {buffer.data, buffer.byteCount}, &image));
        }
    }
    size_t bytes;
    size_t rowBytes;
    if (status == BL_OK &&
        (!image.width || !image.height || image.width > INT32_MAX || image.height > INT32_MAX ||
         !l_size(image.width, 4, &rowBytes) || image.rowStrideBytes < rowBytes ||
         !l_size(image.rowStrideBytes, image.height, &bytes) || !image.rgba8))
    {
        status = BL_HOST_ERROR;
    }
    if (status == BL_OK)
    {
        size_t subdivisions =
            o && o->ground.subdivisions.present ? (size_t)o->ground.subdivisions.value : 1;
        for (size_t row = 0; row <= subdivisions; ++row)
        {
            size_t y = (size_t)(((double)row / subdivisions) * (image.height - 1));
            for (size_t col = 0; col <= subdivisions; ++col)
            {
                size_t x = (size_t)(((double)col / subdivisions) * (image.width - 1));
                const uint8_t* pixel = image.rgba8 + y * image.rowStrideBytes + x * 4;
                double gradient =
                    pixel[0] / 255.0 * .3 + pixel[1] / 255.0 * .59 + pixel[2] / 255.0 * .11;
                data.positions[(row * (subdivisions + 1) + col) * 3 + 1] =
                    (float)(low + gradient * (high - low));
            }
        }
        heightNormals(&data);
    }
    if (decoded)
    {
        l_enterHost(r);
        r->io.releaseImage(r->io.userData, &image);
        l_leaveHost(r);
    }
    l_enterHost(r);
    r->io.releaseBuffer(r->io.userData, &buffer);
    l_leaveHost(r);
    if (status != BL_OK)
    {
        bl_freeGeometryData(r, &data);
        return status;
    }
    *out = data;
    return BL_OK;
}
