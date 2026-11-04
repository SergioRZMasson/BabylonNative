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
    l_free(runtime, mesh->instanceMatrices64);
    mesh->instanceMatrices64 = NULL;
    mesh->instanceMatrices = copied;
    mesh->instanceFloatCount = matrices.count;
    mesh->instanceCount = count;
    mesh->instanceCapacity = count;
    mesh->hasInstances = true;
    mesh->instanceDirtyMin = 0;
    mesh->instanceDirtyMax = count;
    ++mesh->instanceVersion;
    l_publishThinState(runtime, mesh);
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
    if (mesh->instanceMatrices64)
    {
        return BL_UNSUPPORTED;
    }
    *out = {{mesh->instanceMatrices, mesh->instanceFloatCount},
            mesh->instanceCount,
            mesh->instanceCapacity,
            mesh->instanceVersion,
            mesh->instanceWorldBounds};
    return BL_OK;
}

bl_Status bl_setThinInstancesF64(bl_Mesh h, bl_NumberSpan matrices, size_t count)
{
    L_GET(h, L_MESH, L_Mesh, mesh);
    size_t bytes;
    if (matrices.count % 16 || count > matrices.count / 16 || count > UINT32_MAX / 80 ||
        !l_typedSpan(matrices.data, matrices.count, alignof(double)) ||
        !l_size(matrices.count, sizeof(double), &bytes) || !l_allocationFits(bytes) ||
        mesh->instanceVersion == UINT64_MAX)
    {
        return BL_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < matrices.count; ++i)
    {
        if (!isfinite(matrices.data[i]) || !isfinite((float)matrices.data[i]))
        {
            return BL_INVALID_ARGUMENT;
        }
    }
    double* copy = (double*)l_alloc(h._runtime, bytes ? bytes : sizeof(double));
    if (!copy)
    {
        return BL_OUT_OF_MEMORY;
    }
    if (bytes)
    {
        memcpy(copy, matrices.data, bytes);
    }
    l_free(h._runtime, mesh->instanceMatrices);
    l_free(h._runtime, mesh->instanceMatrices64);
    mesh->instanceMatrices = NULL;
    mesh->instanceMatrices64 = copy;
    mesh->instanceFloatCount = matrices.count;
    mesh->instanceCount = count;
    mesh->instanceCapacity = count;
    if (!mesh->hasInstances)
    {
        mesh->instanceBufferIndex = BL_INVALID_BGFX_HANDLE;
    }
    mesh->hasInstances = true;
    mesh->instanceDirtyMin = 0;
    mesh->instanceDirtyMax = count;
    ++mesh->instanceVersion;
    l_publishThinState(h._runtime, mesh);
    return BL_OK;
}

bl_Status bl_getThinInstances2(bl_Mesh h, bl_ThinInstanceView2* out)
{
    L_GET(h, L_MESH, L_Mesh, mesh);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    if (!mesh->hasInstances)
    {
        return BL_NOT_READY;
    }
    bl_ThinInstanceView2 view = {};
    view.base = {{mesh->instanceMatrices, mesh->instanceMatrices ? mesh->instanceFloatCount : 0},
                 mesh->instanceCount,
                 mesh->instanceCapacity,
                 mesh->instanceVersion,
                 mesh->instanceWorldBounds};
    view.matrices64 = {mesh->instanceMatrices64,
                       mesh->instanceMatrices64 ? mesh->instanceFloatCount : 0};
    view.colors = {mesh->instanceColors, mesh->instanceColorCount};
    view.colorVersion = mesh->instanceColorVersion;
    *out = view;
    return BL_OK;
}

bl_Status bl_setThinInstanceMatrix(bl_Mesh h, size_t index, const bl_Mat4* matrix)
{
    L_GET(h, L_MESH, L_Mesh, mesh);
    if (!mesh->hasInstances)
    {
        return BL_NOT_READY;
    }
    if (!matrix || index >= mesh->instanceFloatCount / 16 || mesh->instanceVersion == UINT64_MAX)
    {
        return BL_INVALID_ARGUMENT;
    }
    for (unsigned i = 0; i < 16; ++i)
    {
        if (!isfinite(matrix->values[i]) || !isfinite((float)matrix->values[i]))
        {
            return BL_INVALID_ARGUMENT;
        }
    }
    if (mesh->instanceMatrices64)
    {
        memcpy(mesh->instanceMatrices64 + index * 16, matrix->values, sizeof(*matrix));
    }
    else
    {
        for (unsigned i = 0; i < 16; ++i)
        {
            mesh->instanceMatrices[index * 16 + i] = (float)matrix->values[i];
        }
    }
    if (index < mesh->instanceDirtyMin)
    {
        mesh->instanceDirtyMin = index;
    }
    if (index + 1 > mesh->instanceDirtyMax)
    {
        mesh->instanceDirtyMax = index + 1;
    }
    ++mesh->instanceVersion;
    l_publishThinState(h._runtime, mesh);
    return BL_OK;
}

bl_Status bl_setThinInstanceColors(bl_Mesh h, bl_F32Span colors)
{
    L_GET(h, L_MESH, L_Mesh, mesh);
    if (!mesh->hasInstances)
    {
        return BL_NOT_READY;
    }
    size_t bytes;
    if (colors.count % 4 || colors.count / 4 < mesh->instanceCount ||
        !l_typedSpan(colors.data, colors.count, alignof(float)) ||
        !l_size(colors.count, sizeof(float), &bytes) || !l_allocationFits(bytes) ||
        mesh->instanceVersion == UINT64_MAX || mesh->instanceColorVersion == UINT64_MAX)
    {
        return BL_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < colors.count; ++i)
    {
        if (!isfinite(colors.data[i]))
        {
            return BL_INVALID_ARGUMENT;
        }
    }
    float* copy = (float*)l_alloc(h._runtime, bytes ? bytes : sizeof(float));
    if (!copy)
    {
        return BL_OUT_OF_MEMORY;
    }
    if (bytes)
    {
        memcpy(copy, colors.data, bytes);
    }
    l_free(h._runtime, mesh->instanceColors);
    mesh->instanceColors = copy;
    mesh->instanceColorCount = colors.count;
    ++mesh->instanceColorVersion;
    ++mesh->instanceVersion;
    mesh->instanceColorDirtyMin = 0;
    mesh->instanceColorDirtyMax = mesh->instanceCount;
    l_publishThinState(h._runtime, mesh);
    return BL_OK;
}

bl_Status bl_setThinInstanceCount(bl_Mesh h, size_t count)
{
    L_GET(h, L_MESH, L_Mesh, mesh);
    if (!mesh->hasInstances)
    {
        return BL_NOT_READY;
    }
    if (count > mesh->instanceCapacity || count > mesh->instanceFloatCount / 16 ||
        (mesh->instanceColors && count > mesh->instanceColorCount / 4) ||
        mesh->instanceVersion == UINT64_MAX)
    {
        return BL_INVALID_ARGUMENT;
    }
    mesh->instanceCount = count;
    mesh->instanceDirtyMin = 0;
    mesh->instanceDirtyMax = count;
    ++mesh->instanceVersion;
    l_publishThinState(h._runtime, mesh);
    return BL_OK;
}

bl_Status bl_flushThinInstances(bl_Mesh h)
{
    L_GET(h, L_MESH, L_Mesh, mesh);
    return bl_setThinInstanceCount(h, mesh->instanceCount);
}

bl_Status bl_writeThinInstanceData(bl_Mesh h, const bl_ThinInstanceUpdate* update)
{
    L_GET(h, L_MESH, L_Mesh, mesh);
    if (!mesh->hasInstances)
    {
        return BL_NOT_READY;
    }
    if (!update || update->count > mesh->instanceCapacity ||
        update->count > mesh->instanceFloatCount / 16 || mesh->instanceVersion == UINT64_MAX ||
        (update->writeColors && mesh->instanceColorVersion == UINT64_MAX))
    {
        return BL_INVALID_ARGUMENT;
    }
    if (update->writeMatrices)
    {
        bool precise = mesh->instanceMatrices64 != NULL;
        size_t count = precise ? update->matrices64.count : update->matrices.count;
        const void* values = precise ? (const void*)update->matrices64.data : update->matrices.data;
        size_t alignment = precise ? alignof(double) : alignof(float);
        if (count != mesh->instanceFloatCount ||
            (precise ? update->matrices.count : update->matrices64.count) ||
            !l_typedSpan(values, count, alignment))
        {
            return BL_INVALID_ARGUMENT;
        }
        for (size_t i = 0; i < count; ++i)
        {
            double value = precise ? update->matrices64.data[i] : update->matrices.data[i];
            if (!isfinite(value) || !isfinite((float)value))
            {
                return BL_INVALID_ARGUMENT;
            }
        }
    }
    size_t colorBytes = 0;
    float* colors = mesh->instanceColors;
    if (update->writeColors)
    {
        if (update->colors.count % 4 || update->colors.count / 4 < update->count ||
            !l_typedSpan(update->colors.data, update->colors.count, alignof(float)) ||
            !l_size(update->colors.count, sizeof(float), &colorBytes) ||
            !l_allocationFits(colorBytes))
        {
            return BL_INVALID_ARGUMENT;
        }
        for (size_t i = 0; i < update->colors.count; ++i)
        {
            if (!isfinite(update->colors.data[i]))
            {
                return BL_INVALID_ARGUMENT;
            }
        }
        uintptr_t source = (uintptr_t)update->colors.data;
        uintptr_t matrices = (uintptr_t)(mesh->instanceMatrices64 ? (void*)mesh->instanceMatrices64
                                                                  : mesh->instanceMatrices);
        size_t matrixBytes =
            mesh->instanceFloatCount * (mesh->instanceMatrices64 ? sizeof(double) : sizeof(float));
        bool aliases = update->writeMatrices && colorBytes && source < matrices + matrixBytes &&
                       matrices < source + colorBytes;
        if (update->colors.count != mesh->instanceColorCount || !colors || aliases)
        {
            colors = (float*)l_alloc(h._runtime, colorBytes ? colorBytes : sizeof(float));
            if (!colors)
            {
                return BL_OUT_OF_MEMORY;
            }
            if (colorBytes)
            {
                memmove(colors, update->colors.data, colorBytes);
            }
        }
    }
    else if (colors && update->count > mesh->instanceColorCount / 4)
    {
        return BL_INVALID_ARGUMENT;
    }
    if (update->writeMatrices && mesh->instanceFloatCount)
    {
        if (mesh->instanceMatrices64)
        {
            memmove(mesh->instanceMatrices64, update->matrices64.data,
                    mesh->instanceFloatCount * sizeof(double));
        }
        else
        {
            memmove(mesh->instanceMatrices, update->matrices.data,
                    mesh->instanceFloatCount * sizeof(float));
        }
    }
    if (update->writeColors)
    {
        if (colorBytes && colors == mesh->instanceColors)
        {
            memmove(colors, update->colors.data, colorBytes);
        }
        if (colors != mesh->instanceColors)
        {
            l_free(h._runtime, mesh->instanceColors);
        }
        mesh->instanceColors = colors;
        mesh->instanceColorCount = update->colors.count;
        mesh->instanceColorDirtyMin = 0;
        mesh->instanceColorDirtyMax = update->count;
        ++mesh->instanceColorVersion;
    }
    if (update->writeMatrices || mesh->instanceCount != update->count)
    {
        mesh->instanceDirtyMin = 0;
        mesh->instanceDirtyMax = update->count;
    }
    mesh->instanceCount = update->count;
    ++mesh->instanceVersion;
    l_publishThinState(h._runtime, mesh);
    return BL_OK;
}

bl_Status bl_enableThinInstanceGpuCulling(bl_Mesh h, bool enabled)
{
    L_GET(h, L_MESH, L_Mesh, mesh);
    if (!mesh->hasInstances)
    {
        return l_error(h._runtime, BL_INVALID_ARGUMENT, __func__,
                       "GPU culling requires installed thin instances", 494);
    }
    if (mesh->instanceCulling != enabled)
    {
        mesh->instanceCulling = enabled;
        mesh->instanceGpuVersion = 0;
        mesh->instanceDirtyMin = 0;
        mesh->instanceDirtyMax = mesh->instanceCount;
        mesh->instanceColorDirtyMin = 0;
        mesh->instanceColorDirtyMax = mesh->instanceCount;
        l_publishThinState(h._runtime, mesh);
    }
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
        double converted[16];
        const float* matrix =
            mesh->hasInstances && mesh->instanceWorldBounds && !mesh->instanceMatrices64
                ? mesh->instanceMatrices + instance * 16
                : NULL;
        const double* precise =
            mesh->hasInstances && mesh->instanceWorldBounds && mesh->instanceMatrices64
                ? mesh->instanceMatrices64 + instance * 16
                : NULL;
        if (matrix)
        {
            for (unsigned i = 0; i < 16; ++i)
            {
                converted[i] = matrix[i];
            }
            precise = converted;
        }
        if (precise)
        {
            double linear = 0;
            for (unsigned row = 0; row < 3; ++row)
            {
                for (unsigned column = 0; column < 3; ++column)
                {
                    linear += fabs(precise[column * 4 + row]);
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
                if (precise)
                {
                    coefficient = 0;
                    for (unsigned inner = 0; inner < 3; ++inner)
                    {
                        coefficient += world[inner * 4 + row] * precise[column * 4 + inner];
                    }
                }
                transformedCenter += coefficient * center[column];
                transformedRadius += fabs(coefficient) * extent[column];
            }
            if (precise)
            {
                for (unsigned inner = 0; inner < 3; ++inner)
                {
                    transformedCenter += world[inner * 4 + row] * precise[12 + inner];
                }
            }
            expandAxis(minimum, maximum, row, transformedCenter, transformedRadius);
        }
        *hasBounds = true;
    }
    return BL_OK;
}
