#include "LiteInternal.h"

bl_Status l_prepareThinBuffer(bl_Runtime* r, L_Mesh* mesh, bool colors)
{
    if (!mesh->hasInstances || !mesh->instanceCount)
    {
        return BL_OK;
    }
    if (bgfx::getCaps()->limits.maxInstanceData < (colors ? 5u : 4u))
    {
        return BL_UNSUPPORTED;
    }
    if (mesh->instanceCount > mesh->instanceFloatCount / 16 ||
        (colors && mesh->instanceColorCount / 4 < mesh->instanceCount))
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Thin-instance colour/matrix slab is too short");
    }
    uint16_t stride = colors ? 80 : 64;
    size_t capacity = mesh->instanceCapacity;
    if (capacity < mesh->instanceCount || capacity > UINT32_MAX / stride)
    {
        return BL_INVALID_ARGUMENT;
    }
    bool recreate = mesh->instanceBufferIndex == BL_INVALID_BGFX_HANDLE ||
                    mesh->instanceGpuStride != stride ||
                    mesh->instanceGpuCapacity < mesh->instanceCount;
    size_t minimum = recreate ? 0 : mesh->instanceDirtyMin;
    size_t maximum = recreate ? mesh->instanceCount : mesh->instanceDirtyMax;
    if (colors)
    {
        if (mesh->instanceColorDirtyMin < minimum)
        {
            minimum = mesh->instanceColorDirtyMin;
        }
        if (mesh->instanceColorDirtyMax > maximum)
        {
            maximum = mesh->instanceColorDirtyMax;
        }
    }
    if (maximum > mesh->instanceCount)
    {
        maximum = mesh->instanceCount;
    }
    if (!recreate && mesh->instanceGpuVersion == mesh->instanceVersion)
    {
        return BL_OK;
    }
    if (maximum > minimum)
    {
        L_TRY(l_validateStorageUploadViews(mesh->engine, 1));
    }
    bool pack = colors || mesh->instanceMatrices64;
    size_t floats = capacity * (stride / 4);
    if (pack && mesh->instanceUploadFloats < floats)
    {
        size_t bytes;
        if (!l_size(floats, sizeof(float), &bytes) || !l_allocationFits(bytes))
        {
            return BL_INVALID_ARGUMENT;
        }
        float* scratch = (float*)l_alloc(r, bytes);
        if (!scratch)
        {
            return BL_OUT_OF_MEMORY;
        }
        l_free(r, mesh->instanceUpload);
        mesh->instanceUpload = scratch;
        mesh->instanceUploadFloats = floats;
        l_publishThinState(r, mesh);
    }
    if (pack)
    {
        for (size_t i = minimum; i < maximum; ++i)
        {
            float* target = mesh->instanceUpload + i * (stride / 4);
            for (unsigned column = 0; column < 16; ++column)
            {
                target[column] = mesh->instanceMatrices64
                                     ? (float)mesh->instanceMatrices64[i * 16 + column]
                                     : mesh->instanceMatrices[i * 16 + column];
            }
            if (colors)
            {
                memcpy(target + 16, mesh->instanceColors + i * 4, 4 * sizeof(float));
            }
        }
    }
    uint16_t handle = mesh->instanceBufferIndex;
    if (recreate)
    {
        bgfx::VertexLayout layout;
        layout.begin().skip((uint8_t)stride).end();
        handle = bgfx::createDynamicVertexBuffer((uint32_t)capacity, layout,
                                                 BGFX_BUFFER_COMPUTE_READ_WRITE)
                     .idx;
        if (handle == BL_INVALID_BGFX_HANDLE)
        {
            return BL_OUT_OF_MEMORY;
        }
    }
    if (maximum > minimum)
    {
        const float* values = pack ? mesh->instanceUpload : mesh->instanceMatrices;
        bgfx::BufferRegion destination;
        destination.init(bgfx::DynamicVertexBufferHandle{handle}, (uint32_t)(minimum * stride),
                         (uint32_t)((maximum - minimum) * stride));
        L_StorageUpload upload;
        bl_Status status =
            l_prepareStorageUpload(&destination, values + minimum * (stride / 4), &upload);
        if (status == BL_OK)
        {
            status = l_submitStorageUploads(mesh->engine, &upload, 1);
        }
        if (status != BL_OK)
        {
            if (recreate)
            {
                bgfx::destroy(bgfx::DynamicVertexBufferHandle{handle});
            }
            return status;
        }
    }
    if (recreate && mesh->instanceBufferIndex != BL_INVALID_BGFX_HANDLE)
    {
        bgfx::destroy(bgfx::DynamicVertexBufferHandle{mesh->instanceBufferIndex});
    }
    mesh->instanceBufferIndex = handle;
    mesh->instanceGpuStride = stride;
    if (recreate)
    {
        mesh->instanceGpuCapacity = capacity;
    }
    mesh->instanceDirtyMin = mesh->instanceCount;
    mesh->instanceDirtyMax = 0;
    mesh->instanceColorDirtyMin = mesh->instanceCount;
    mesh->instanceColorDirtyMax = 0;
    mesh->instanceGpuVersion = mesh->instanceVersion;
    l_publishThinState(r, mesh);
    return BL_OK;
}
