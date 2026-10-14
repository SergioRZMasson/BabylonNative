#include "LiteInternal.h"

bl_Status bl_setThinInstances(bl_Mesh handle, bl_F32Span matrices, size_t count)
{
    L_GET(handle, L_MESH, L_Mesh, mesh);
    bl_Runtime* runtime = handle._runtime;
    size_t bytes;
    if (matrices.count % 16 || count > matrices.count / 16 || count > UINT32_MAX ||
        !l_typedSpan(matrices.data, matrices.count, alignof(float)) ||
        !l_size(matrices.count, sizeof(float), &bytes) || bytes > UINT32_MAX ||
        !l_allocationFits(bytes) || count > UINT32_MAX / 64 || mesh->instanceVersion == UINT64_MAX)
    {
        return L_FAIL(runtime, BL_INVALID_ARGUMENT, "Invalid thin-instance matrix span/count");
    }
    for (size_t i = 0; i < matrices.count; ++i)
    {
        if (!isfinite(matrices.data[i]))
        {
            return L_FAIL(runtime, BL_INVALID_ARGUMENT, "Nonfinite thin-instance matrix");
        }
    }
    float* copied = (float*)l_alloc(runtime, bytes ? bytes : sizeof(float));
    if (!copied)
    {
        return BL_OUT_OF_MEMORY;
    }
    if (bytes)
    {
        memcpy(copied, matrices.data, bytes);
    }
    if (!mesh->hasInstances)
    {
        mesh->instanceBufferIndex = BL_INVALID_BGFX_HANDLE;
    }
    l_free(runtime, mesh->instanceMatrices);
    mesh->instanceMatrices = copied;
    mesh->instanceFloatCount = matrices.count;
    mesh->instanceCount = count;
    mesh->instanceCapacity = count;
    mesh->hasInstances = true;
    ++mesh->instanceVersion;
    return BL_OK;
}

bl_Status bl_getThinInstances(bl_Mesh handle, bl_ThinInstanceView* out)
{
    L_GET(handle, L_MESH, L_Mesh, mesh);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    if (!mesh->hasInstances)
    {
        return BL_NOT_READY;
    }
    *out = {{mesh->instanceMatrices, mesh->instanceFloatCount},
            mesh->instanceCount,
            mesh->instanceCapacity,
            mesh->instanceVersion,
            mesh->instanceWorldBounds};
    return BL_OK;
}

bl_Status bl_enableThinInstanceWorldBounds(bl_Mesh handle)
{
    L_GET(handle, L_MESH, L_Mesh, mesh);
    if (!mesh->hasInstances)
    {
        return l_error(handle._runtime, BL_INVALID_ARGUMENT, __func__,
                       "Thin-instance bounds require installed instances", 453);
    }
    mesh->instanceWorldBounds = true;
    return BL_OK;
}

static void expandAxis(bl_Vec3* minimum, bl_Vec3* maximum, unsigned row, double center,
                       double extent)
{
    double* low = row == 0 ? &minimum->x : row == 1 ? &minimum->y : &minimum->z;
    double* high = row == 0 ? &maximum->x : row == 1 ? &maximum->y : &maximum->z;
    double lower = center - extent;
    double upper = center + extent;
    if (lower < *low)
    {
        *low = lower;
    }
    if (upper > *high)
    {
        *high = upper;
    }
}

bl_Status l_meshWorldBounds(bl_Runtime* runtime, L_Mesh* mesh, bl_Vec3* minimum, bl_Vec3* maximum,
                            bool* hasBounds)
{
    L_TRY(l_world(runtime, &mesh->node));
    if (!mesh->geometry.vertices)
    {
        return BL_OK;
    }
    const double* world = mesh->node.world.values;
    const bl_Vec3* low = &mesh->geometry.minimum;
    const bl_Vec3* high = &mesh->geometry.maximum;
    double center[3] = {(low->x + high->x) * 0.5, (low->y + high->y) * 0.5,
                        (low->z + high->z) * 0.5};
    double extent[3] = {(high->x - low->x) * 0.5, (high->y - low->y) * 0.5,
                        (high->z - low->z) * 0.5};
    size_t count = mesh->hasInstances && mesh->instanceWorldBounds ? mesh->instanceCount : 1;
    for (size_t instance = 0; instance < count; ++instance)
    {
        const float* matrix = mesh->hasInstances && mesh->instanceWorldBounds
                                  ? mesh->instanceMatrices + instance * 16
                                  : NULL;
        if (matrix)
        {
            double linear = 0;
            for (unsigned row = 0; row < 3; ++row)
            {
                for (unsigned column = 0; column < 3; ++column)
                {
                    linear += fabs(matrix[column * 4 + row]);
                }
            }
            if (linear < 1e-9)
            {
                continue;
            }
        }
        for (unsigned row = 0; row < 3; ++row)
        {
            double transformedCenter = world[12 + row];
            double transformedRadius = 0;
            for (unsigned column = 0; column < 3; ++column)
            {
                double coefficient = world[column * 4 + row];
                if (matrix)
                {
                    coefficient = 0;
                    for (unsigned inner = 0; inner < 3; ++inner)
                    {
                        coefficient += world[inner * 4 + row] * matrix[column * 4 + inner];
                    }
                }
                transformedCenter += coefficient * center[column];
                transformedRadius += fabs(coefficient) * extent[column];
            }
            if (matrix)
            {
                for (unsigned inner = 0; inner < 3; ++inner)
                {
                    transformedCenter += world[inner * 4 + row] * matrix[12 + inner];
                }
            }
            expandAxis(minimum, maximum, row, transformedCenter, transformedRadius);
        }
        *hasBounds = true;
    }
    return BL_OK;
}
