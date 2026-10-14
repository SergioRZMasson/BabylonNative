#include "AssetInternal.h"

static bool descriptorSpan(const void* data, size_t count, size_t size, size_t alignment)
{
    size_t bytes;
    return count <= UINT32_MAX && l_size(count, size, &bytes) && l_allocationFits(bytes) &&
           l_typedSpan(data, count, alignment);
}

size_t l_assetViewOffset(const bl_ParsedGltf* data, uint32_t index, const bl_ParsedGltf4* full)
{
    return !full || full->bufferViewInputs[index].hasByteOffset
               ? data->bufferViews[index].byteOffset
               : 0;
}

static size_t componentWidth(bl_AssetComponentType type)
{
    switch (type)
    {
        case BL_ASSET_COMPONENT_I8:
        case BL_ASSET_COMPONENT_U8:
        {
            return 1;
        }
        case BL_ASSET_COMPONENT_I16:
        case BL_ASSET_COMPONENT_U16:
        {
            return 2;
        }
        case BL_ASSET_COMPONENT_U32:
        case BL_ASSET_COMPONENT_F32:
        {
            return 4;
        }
        default:
        {
            return 0;
        }
    }
}

static uint32_t unsignedValue(const uint8_t* data, size_t width)
{
    uint32_t value = data[0];
    for (size_t i = 1; i < width; ++i)
    {
        value |= (uint32_t)data[i] << (i * 8);
    }
    return value;
}

static double componentValue(const uint8_t* data, bl_AssetComponentType type, bool normalized)
{
    size_t width = componentWidth(type);
    uint32_t bits = unsignedValue(data, width);
    double value;
    if (type == BL_ASSET_COMPONENT_F32)
    {
        float number;
        memcpy(&number, &bits, sizeof(number));
        return number;
    }
    if (type == BL_ASSET_COMPONENT_I8)
    {
        value = bits < 128 ? (double)bits : (double)bits - 256;
    }
    else if (type == BL_ASSET_COMPONENT_I16)
    {
        value = bits < 32768 ? (double)bits : (double)bits - 65536;
    }
    else
    {
        value = bits;
    }
    if (normalized)
    {
        double maximum = type == BL_ASSET_COMPONENT_I8    ? 127
                         : type == BL_ASSET_COMPONENT_U8  ? 255
                         : type == BL_ASSET_COMPONENT_I16 ? 32767
                                                          : 65535;
        value *= 1 / maximum;
        if (value < -1)
        {
            value = -1;
        }
    }
    return value;
}

static bl_Status viewBytes(const bl_ParsedGltf* data, uint32_t index, size_t offset, size_t bytes,
                           size_t width, const bl_ParsedGltf4* full, const uint8_t** out)
{
    if (index >= data->bufferViewCount)
    {
        return BL_INVALID_ARGUMENT;
    }
    const bl_AssetBufferView* view = data->bufferViews + index;
    if (view->buffer >= data->bufferCount)
    {
        return BL_INVALID_ARGUMENT;
    }
    const bl_AssetBuffer* buffer = data->buffers + view->buffer;
    size_t base = l_assetViewOffset(data, index, full);
    if (!l_span(buffer->bytes.data, buffer->bytes.count) ||
        buffer->declaredByteLength > buffer->bytes.count || base > buffer->declaredByteLength ||
        view->byteLength > buffer->declaredByteLength - base || offset > view->byteLength ||
        bytes > view->byteLength - offset || base % width || offset % width ||
        (uintptr_t)buffer->bytes.data > UINTPTR_MAX - buffer->declaredByteLength ||
        (uintptr_t)buffer->bytes.data % width)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = buffer->bytes.data ? buffer->bytes.data + base + offset : NULL;
    return BL_OK;
}

bl_Status l_assetAccessorReader(const bl_ParsedGltf* data, uint32_t index,
                                const bl_ParsedGltf4* full, L_AssetAccessorReader* out)
{
    if (!out || index >= data->accessorCount ||
        !descriptorSpan(data->accessors, data->accessorCount, sizeof(bl_AssetAccessor),
                        alignof(bl_AssetAccessor)) ||
        !descriptorSpan(data->bufferViews, data->bufferViewCount, sizeof(bl_AssetBufferView),
                        alignof(bl_AssetBufferView)) ||
        !descriptorSpan(data->buffers, data->bufferCount, sizeof(bl_AssetBuffer),
                        alignof(bl_AssetBuffer)) ||
        (full &&
         (!descriptorSpan(full->accessorInputs, data->accessorCount, sizeof(bl_AssetAccessorInputs),
                          alignof(bl_AssetAccessorInputs)) ||
          !descriptorSpan(full->bufferViewInputs, data->bufferViewCount,
                          sizeof(bl_AssetBufferViewInputs), alignof(bl_AssetBufferViewInputs)))))
    {
        return BL_INVALID_ARGUMENT;
    }
    L_AssetAccessorReader reader = {};
    const bl_AssetAccessor* accessor = data->accessors + index;
    const bl_AssetAccessorInputs* inputs = full ? full->accessorInputs + index : NULL;
    reader.accessor = accessor;
    reader.width = componentWidth(accessor->componentType);
    reader.normalized = (!inputs || inputs->hasNormalized) && accessor->normalized;
    if (!reader.width || (unsigned)accessor->shape > BL_ASSET_MAT4 ||
        accessor->count > UINT32_MAX || (full && !accessor->count) ||
        (reader.normalized && (accessor->componentType == BL_ASSET_COMPONENT_F32 ||
                               accessor->componentType == BL_ASSET_COMPONENT_U32)))
    {
        return BL_INVALID_ARGUMENT;
    }
    reader.columns = accessor->shape == BL_ASSET_MAT2   ? 2
                     : accessor->shape == BL_ASSET_MAT3 ? 3
                     : accessor->shape == BL_ASSET_MAT4 ? 4
                                                        : 1;
    reader.rows = reader.columns > 1                 ? reader.columns
                  : accessor->shape == BL_ASSET_VEC4 ? 4
                  : accessor->shape == BL_ASSET_VEC3 ? 3
                  : accessor->shape == BL_ASSET_VEC2 ? 2
                                                     : 1;
    reader.columnBytes = reader.rows * reader.width;
    if (reader.columns > 1)
    {
        reader.columnBytes = (reader.columnBytes + 3) & ~(size_t)3;
    }
    reader.elementBytes = reader.columns * reader.columnBytes;
    reader.stride = reader.elementBytes;
    size_t offset = !inputs || inputs->hasByteOffset ? accessor->byteOffset : 0;
    if (accessor->bufferView.present)
    {
        if (accessor->bufferView.value >= data->bufferViewCount)
        {
            return BL_INVALID_ARGUMENT;
        }
        const bl_AssetBufferView* view = data->bufferViews + accessor->bufferView.value;
        if (view->byteStride.present)
        {
            reader.stride = view->byteStride.value;
            if (reader.stride < reader.elementBytes || reader.stride < 4 || reader.stride > 252 ||
                reader.stride % 4)
            {
                return BL_INVALID_ARGUMENT;
            }
        }
        size_t bytes;
        if (!l_size(accessor->count ? accessor->count - 1 : 0, reader.stride, &bytes) ||
            (accessor->count && reader.elementBytes > SIZE_MAX - bytes))
        {
            return BL_INVALID_ARGUMENT;
        }
        bytes += accessor->count ? reader.elementBytes : 0;
        L_TRY(viewBytes(data, accessor->bufferView.value, offset, bytes, reader.width, full,
                        &reader.base));
        if (reader.columns > 1 && (uintptr_t)reader.base % 4)
        {
            return BL_INVALID_ARGUMENT;
        }
    }
    else if ((inputs && inputs->hasByteOffset) || (!inputs && offset))
    {
        return BL_INVALID_ARGUMENT;
    }
    if (accessor->sparsePresent)
    {
        if (!inputs)
        {
            return BL_UNSUPPORTED;
        }
        const bl_AssetSparseAccessor* sparse = &inputs->sparse;
        if (!sparse->count || sparse->count > accessor->count ||
            sparse->indicesBufferView >= data->bufferViewCount ||
            sparse->valuesBufferView >= data->bufferViewCount ||
            (sparse->indicesComponentType != BL_ASSET_COMPONENT_U8 &&
             sparse->indicesComponentType != BL_ASSET_COMPONENT_U16 &&
             sparse->indicesComponentType != BL_ASSET_COMPONENT_U32))
        {
            return BL_INVALID_ARGUMENT;
        }
        const bl_AssetBufferView* indices = data->bufferViews + sparse->indicesBufferView;
        const bl_AssetBufferView* values = data->bufferViews + sparse->valuesBufferView;
        if (indices->byteStride.present || values->byteStride.present ||
            full->bufferViewInputs[sparse->indicesBufferView].target.present ||
            full->bufferViewInputs[sparse->valuesBufferView].target.present)
        {
            return BL_INVALID_ARGUMENT;
        }
        reader.sparseCount = sparse->count;
        reader.indexWidth = componentWidth(sparse->indicesComponentType);
        size_t bytes;
        if (!l_size(sparse->count, reader.indexWidth, &bytes))
        {
            return BL_INVALID_ARGUMENT;
        }
        L_TRY(viewBytes(data, sparse->indicesBufferView,
                        sparse->indicesByteOffset.present ? sparse->indicesByteOffset.value : 0,
                        bytes, reader.indexWidth, full, &reader.sparseIndices));
        if (!l_size(sparse->count, reader.elementBytes, &bytes))
        {
            return BL_INVALID_ARGUMENT;
        }
        L_TRY(viewBytes(data, sparse->valuesBufferView,
                        sparse->valuesByteOffset.present ? sparse->valuesByteOffset.value : 0,
                        bytes, reader.width, full, &reader.sparseValues));
        if (reader.columns > 1 && (uintptr_t)reader.sparseValues % 4)
        {
            return BL_INVALID_ARGUMENT;
        }
        uint32_t previous = 0;
        for (size_t i = 0; i < sparse->count; ++i)
        {
            uint32_t value =
                unsignedValue(reader.sparseIndices + i * reader.indexWidth, reader.indexWidth);
            if (value >= accessor->count || (i && value <= previous))
            {
                return BL_INVALID_ARGUMENT;
            }
            previous = value;
        }
    }
    *out = reader;
    return BL_OK;
}

bl_Status l_assetAccessorElement(const L_AssetAccessorReader* reader, size_t element,
                                 double* values, bool normalize)
{
    if (!reader || !reader->accessor || !values || element >= reader->accessor->count)
    {
        return BL_INVALID_ARGUMENT;
    }
    const uint8_t* source = reader->base ? reader->base + element * reader->stride : NULL;
    size_t first = 0;
    size_t end = reader->sparseCount;
    while (first < end)
    {
        size_t middle = first + (end - first) / 2;
        uint32_t index =
            unsignedValue(reader->sparseIndices + middle * reader->indexWidth, reader->indexWidth);
        if (index < element)
        {
            first = middle + 1;
        }
        else
        {
            end = middle;
        }
    }
    if (first < reader->sparseCount &&
        unsignedValue(reader->sparseIndices + first * reader->indexWidth, reader->indexWidth) ==
            element)
    {
        source = reader->sparseValues + first * reader->elementBytes;
    }
    for (size_t column = 0; column < reader->columns; ++column)
    {
        for (size_t row = 0; row < reader->rows; ++row)
        {
            values[column * reader->rows + row] =
                source ? componentValue(source + column * reader->columnBytes + row * reader->width,
                                        reader->accessor->componentType,
                                        normalize && reader->normalized)
                       : 0;
        }
    }
    return BL_OK;
}

static bool boundValue(double value, bl_AssetComponentType type)
{
    if (!isfinite(value))
    {
        return false;
    }
    if (type == BL_ASSET_COMPONENT_F32)
    {
        return isfinite((float)value);
    }
    double minimum = type == BL_ASSET_COMPONENT_I8    ? -128
                     : type == BL_ASSET_COMPONENT_I16 ? -32768
                                                      : 0;
    double maximum = type == BL_ASSET_COMPONENT_I8    ? 127
                     : type == BL_ASSET_COMPONENT_U8  ? 255
                     : type == BL_ASSET_COMPONENT_I16 ? 32767
                     : type == BL_ASSET_COMPONENT_U16 ? 65535
                                                      : 4294967295.0;
    return value >= minimum && value <= maximum && floor(value) == value;
}

bl_Status l_validateAssetAccessorBounds(const bl_ParsedGltf* data, const bl_ParsedGltf4* full)
{
    for (size_t i = 0; i < data->accessorCount; ++i)
    {
        L_AssetAccessorReader reader;
        L_TRY(l_assetAccessorReader(data, (uint32_t)i, full, &reader));
        const bl_AssetAccessorInputs* inputs = full->accessorInputs + i;
        size_t components = reader.rows * reader.columns;
        const bl_NumberSpan spans[2] = {inputs->hasMin ? inputs->min : bl_NumberSpan{},
                                        inputs->hasMax ? inputs->max : bl_NumberSpan{}};
        const bool present[2] = {inputs->hasMin, inputs->hasMax};
        for (unsigned bound = 0; bound < 2; ++bound)
        {
            if (present[bound] &&
                (spans[bound].count != components ||
                 !descriptorSpan(spans[bound].data, components, sizeof(double), alignof(double))))
            {
                return BL_INVALID_ARGUMENT;
            }
            for (size_t component = 0; present[bound] && component < components; ++component)
            {
                if (!boundValue(spans[bound].data[component], reader.accessor->componentType))
                {
                    return BL_INVALID_ARGUMENT;
                }
            }
        }
        if (!inputs->hasMin && !inputs->hasMax)
        {
            continue;
        }
        if (!reader.base && !reader.sparseCount)
        {
            continue;
        }
        double minimum[16];
        double maximum[16];
        for (size_t element = 0; element < reader.accessor->count; ++element)
        {
            double values[16];
            L_TRY(l_assetAccessorElement(&reader, element, values, false));
            for (size_t component = 0; component < components; ++component)
            {
                if (!isfinite(values[component]))
                {
                    return BL_INVALID_ARGUMENT;
                }
                if (!element || values[component] < minimum[component])
                {
                    minimum[component] = values[component];
                }
                if (!element || values[component] > maximum[component])
                {
                    maximum[component] = values[component];
                }
            }
        }
        for (size_t component = 0; component < components; ++component)
        {
            double low = inputs->hasMin ? inputs->min.data[component] : minimum[component];
            double high = inputs->hasMax ? inputs->max.data[component] : maximum[component];
            if (reader.accessor->componentType == BL_ASSET_COMPONENT_F32)
            {
                low = (float)low;
                high = (float)high;
            }
            if (low != minimum[component] || high != maximum[component])
            {
                return BL_INVALID_ARGUMENT;
            }
        }
    }
    return BL_OK;
}

static bl_Status sourceIndex(const L_AssetAccessorReader* reader, size_t source, size_t vertices,
                             uint32_t* out)
{
    uint32_t value = (uint32_t)source;
    if (reader)
    {
        double component;
        L_TRY(l_assetAccessorElement(reader, source, &component, false));
        value = (uint32_t)component;
        uint32_t restart = reader->width == 1   ? UINT8_MAX
                           : reader->width == 2 ? UINT16_MAX
                                                : UINT32_MAX;
        if (value == restart)
        {
            return BL_INVALID_ARGUMENT;
        }
    }
    if (value >= vertices)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = value;
    return BL_OK;
}

bl_Status l_assetTriangleIndices(bl_Runtime* runtime, const bl_ParsedGltf* data,
                                 const bl_AssetPrimitive* primitive, size_t vertices,
                                 const bl_ParsedGltf4* full, bl_U32Span* out)
{
    L_AssetAccessorReader reader = {};
    const L_AssetAccessorReader* indexed = NULL;
    size_t count = vertices;
    if (primitive->indices.present)
    {
        L_TRY(l_assetAccessorReader(data, primitive->indices.value, full, &reader));
        if (reader.accessor->shape != BL_ASSET_SCALAR || reader.normalized ||
            (reader.accessor->componentType != BL_ASSET_COMPONENT_U8 &&
             reader.accessor->componentType != BL_ASSET_COMPONENT_U16 &&
             reader.accessor->componentType != BL_ASSET_COMPONENT_U32))
        {
            return BL_INVALID_ARGUMENT;
        }
        indexed = &reader;
        count = reader.accessor->count;
    }
    bl_AssetPrimitiveMode mode = primitive->hasMode ? primitive->mode : BL_ASSET_MODE_TRIANGLES;
    if (mode != BL_ASSET_MODE_TRIANGLES && mode != BL_ASSET_MODE_TRIANGLE_STRIP &&
        mode != BL_ASSET_MODE_TRIANGLE_FAN)
    {
        return BL_UNSUPPORTED;
    }
    if ((mode == BL_ASSET_MODE_TRIANGLES && count % 3) ||
        (mode != BL_ASSET_MODE_TRIANGLES && count < 3))
    {
        return BL_INVALID_ARGUMENT;
    }
    size_t expanded = count;
    size_t bytes;
    if ((mode != BL_ASSET_MODE_TRIANGLES && !l_size(count - 2, 3, &expanded)) ||
        expanded > UINT32_MAX || !l_size(expanded, sizeof(uint32_t), &bytes) ||
        bytes > UINT32_MAX || !l_allocationFits(bytes))
    {
        return BL_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < count; ++i)
    {
        uint32_t value;
        L_TRY(sourceIndex(indexed, i, vertices, &value));
    }
    uint32_t* indices = (uint32_t*)l_alloc(runtime, bytes ? bytes : sizeof(uint32_t));
    if (!indices)
    {
        return BL_OUT_OF_MEMORY;
    }
    for (size_t i = 0; i < expanded; ++i)
    {
        size_t source = i;
        if (mode == BL_ASSET_MODE_TRIANGLE_STRIP)
        {
            size_t triangle = i / 3;
            size_t corner = i % 3;
            source = corner == 0   ? triangle
                     : corner == 1 ? triangle + 1 + triangle % 2
                                   : triangle + 2 - triangle % 2;
        }
        else if (mode == BL_ASSET_MODE_TRIANGLE_FAN)
        {
            source = i % 3 == 0 ? i / 3 + 1 : i % 3 == 1 ? i / 3 + 2 : 0;
        }
        bl_Status status = sourceIndex(indexed, source, vertices, indices + i);
        if (status != BL_OK)
        {
            l_free(runtime, indices);
            return status;
        }
    }
    *out = {indices, expanded};
    return BL_OK;
}
