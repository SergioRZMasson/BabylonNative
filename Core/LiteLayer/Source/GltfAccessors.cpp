#include "GltfInternal.h"

static bool range(size_t offset, size_t stride, size_t count, size_t width, size_t limit)
{
    size_t tail;
    return offset <= limit && width <= limit - offset && count &&
           l_size(count - 1, stride, &tail) && tail <= limit - offset - width;
}

static uint32_t unsignedValue(const uint8_t* bytes, size_t width)
{
    uint32_t result = 0;
    for (size_t i = 0; i < width; ++i)
    {
        result |= (uint32_t)bytes[i] << (8 * i);
    }
    return result;
}

bl_Status l_gltfValidateAccessors(L_Gltf* gltf)
{
    const cgltf_data* data = gltf->data;
    for (size_t i = 0; i < data->buffers_count; ++i)
    {
        if (data->buffers[i].size && !data->buffers[i].data)
        {
            return BL_INVALID_ARGUMENT;
        }
    }
    for (size_t i = 0; i < data->buffer_views_count; ++i)
    {
        const cgltf_buffer_view* view = data->buffer_views + i;
        if (view->has_meshopt_compression || view->extensions_count)
        {
            gltf->reason = "Compressed or extended buffer views are not implemented";
            return BL_UNSUPPORTED;
        }
        if (!view->buffer || view->offset > view->buffer->size ||
            view->size > view->buffer->size - view->offset ||
            (view->stride && (view->stride < 4 || view->stride > 252 || view->stride % 4)))
        {
            return BL_INVALID_ARGUMENT;
        }
    }
    for (size_t i = 0; i < data->accessors_count; ++i)
    {
        const cgltf_accessor* accessor = data->accessors + i;
        size_t width = cgltf_component_size(accessor->component_type);
        size_t element = cgltf_calc_size(accessor->type, accessor->component_type);
        if (!width || !element || accessor->count > UINT32_MAX || !accessor->count ||
            accessor->stride < element || accessor->stride % width || accessor->offset % width ||
            accessor->extensions_count || (!accessor->buffer_view && accessor->offset))
        {
            return BL_INVALID_ARGUMENT;
        }
        if (accessor->buffer_view && (!range(accessor->offset, accessor->stride, accessor->count,
                                             element, accessor->buffer_view->size) ||
                                      accessor->buffer_view->offset % width))
        {
            return BL_INVALID_ARGUMENT;
        }
        if (!accessor->is_sparse)
        {
            continue;
        }
        const cgltf_accessor_sparse* sparse = &accessor->sparse;
        size_t indexWidth = cgltf_component_size(sparse->indices_component_type);
        bool integer = sparse->indices_component_type == cgltf_component_type_r_8u ||
                       sparse->indices_component_type == cgltf_component_type_r_16u ||
                       sparse->indices_component_type == cgltf_component_type_r_32u;
        if (!integer || !sparse->indices_buffer_view || !sparse->values_buffer_view ||
            !sparse->count || sparse->count > accessor->count ||
            sparse->indices_buffer_view->stride || sparse->values_buffer_view->stride ||
            sparse->indices_buffer_view->type != cgltf_buffer_view_type_invalid ||
            sparse->values_buffer_view->type != cgltf_buffer_view_type_invalid ||
            sparse->indices_byte_offset % indexWidth || sparse->values_byte_offset % width ||
            sparse->indices_buffer_view->offset % indexWidth ||
            sparse->values_buffer_view->offset % width ||
            !range(sparse->indices_byte_offset, indexWidth, sparse->count, indexWidth,
                   sparse->indices_buffer_view->size) ||
            !range(sparse->values_byte_offset, element, sparse->count, element,
                   sparse->values_buffer_view->size))
        {
            return BL_INVALID_ARGUMENT;
        }
        const uint8_t* indices =
            cgltf_buffer_view_data(sparse->indices_buffer_view) + sparse->indices_byte_offset;
        uint32_t previous = 0;
        for (size_t j = 0; j < sparse->count; ++j)
        {
            uint32_t index = unsignedValue(indices + j * indexWidth, indexWidth);
            if (index >= accessor->count || (j && index <= previous))
            {
                return BL_INVALID_ARGUMENT;
            }
            previous = index;
        }
    }
    for (size_t i = 0; i < data->accessors_count; ++i)
    {
        const cgltf_accessor* accessor = data->accessors + i;
        size_t components = cgltf_num_components(accessor->type);
        if (!accessor->has_min && !accessor->has_max)
        {
            continue;
        }
        for (size_t j = 0; j < components; ++j)
        {
            if ((accessor->has_min && !isfinite(accessor->min[j])) ||
                (accessor->has_max && !isfinite(accessor->max[j])))
            {
                return BL_INVALID_ARGUMENT;
            }
        }
        if (!accessor->buffer_view && !accessor->is_sparse)
        {
            continue;
        }
        float minimum[16] = {};
        float maximum[16] = {};
        cgltf_accessor raw = *accessor;
        raw.normalized = false;
        for (size_t element = 0; element < raw.count; ++element)
        {
            float values[16] = {};
            L_TRY(l_gltfRead(&raw, element, values, components));
            for (size_t j = 0; j < components; ++j)
            {
                if (!element || values[j] < minimum[j])
                {
                    minimum[j] = values[j];
                }
                if (!element || values[j] > maximum[j])
                {
                    maximum[j] = values[j];
                }
            }
        }
        for (size_t j = 0; j < components; ++j)
        {
            if ((accessor->has_min && accessor->min[j] != minimum[j]) ||
                (accessor->has_max && accessor->max[j] != maximum[j]))
            {
                return BL_INVALID_ARGUMENT;
            }
        }
    }
    return BL_OK;
}

bl_Status l_gltfRead(const cgltf_accessor* accessor, size_t index, float* out, size_t count)
{
    if (index >= accessor->count || count != cgltf_num_components(accessor->type))
    {
        return BL_INVALID_ARGUMENT;
    }
    // The pinned unpack/read sparse helpers use accessor->stride for sparse values.
    // Sparse values are tightly packed even when the base buffer is interleaved.
    cgltf_accessor dense = *accessor;
    dense.is_sparse = false;
    if (accessor->is_sparse)
    {
        const cgltf_accessor_sparse* sparse = &accessor->sparse;
        size_t width = cgltf_component_size(sparse->indices_component_type);
        const uint8_t* indices =
            cgltf_buffer_view_data(sparse->indices_buffer_view) + sparse->indices_byte_offset;
        size_t low = 0;
        size_t high = sparse->count;
        while (low < high)
        {
            size_t middle = low + (high - low) / 2;
            if (unsignedValue(indices + middle * width, width) < index)
            {
                low = middle + 1;
            }
            else
            {
                high = middle;
            }
        }
        if (low < sparse->count && unsignedValue(indices + low * width, width) == index)
        {
            dense.buffer_view = sparse->values_buffer_view;
            dense.offset = sparse->values_byte_offset;
            dense.stride = cgltf_calc_size(accessor->type, accessor->component_type);
            index = low;
        }
    }
    if (!cgltf_accessor_read_float(&dense, index, out, count))
    {
        return BL_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < count; ++i)
    {
        if (!isfinite(out[i]))
        {
            return BL_INVALID_ARGUMENT;
        }
    }
    return BL_OK;
}
