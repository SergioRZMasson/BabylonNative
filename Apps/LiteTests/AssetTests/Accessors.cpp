#include "AssetInternal.h"
#include <initializer_list>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned checks = 0;

static void require(bool value, const char* message)
{
    ++checks;
    if (!value)
    {
        fprintf(stderr, "Accessor numeric: %s\n", message);
        exit(2);
    }
}

struct Fixture
{
    bl_ParsedGltf4 full;
    bl_AssetBuffer buffer;
    bl_AssetBufferView views[3];
    bl_AssetBufferViewInputs viewInputs[3];
    bl_AssetAccessor accessor;
    bl_AssetAccessorInputs inputs;
    uint8_t bytes[256];
    double low[16];
    double high[16];
};

static void init(Fixture* fixture)
{
    *fixture = {};
    auto& data = fixture->full.document3.document;
    data.structSize = sizeof(fixture->full);
    data.version = 4;
    data.gltfVersion = 2;
    data.buffers = &fixture->buffer;
    data.bufferCount = 1;
    data.bufferViews = fixture->views;
    data.bufferViewCount = 3;
    data.accessors = &fixture->accessor;
    data.accessorCount = 1;
    fixture->full.accessorInputs = &fixture->inputs;
    fixture->full.bufferViewInputs = fixture->viewInputs;
    fixture->buffer = {{fixture->bytes, sizeof(fixture->bytes)}, sizeof(fixture->bytes), {}};
    fixture->views[0] = {0, 0, 48, {true, 16}};
    fixture->views[1] = {0, 64, 8, {}};
    fixture->views[2] = {0, 80, 32, {}};
    fixture->viewInputs[1].hasByteOffset = true;
    fixture->viewInputs[2].hasByteOffset = true;
    fixture->accessor = {{true, 0}, 0, 3, BL_ASSET_COMPONENT_F32, BL_ASSET_VEC3, false, false};
}

static bl_Status read(Fixture& fixture, L_AssetAccessorReader* reader)
{
    return l_assetAccessorReader(&fixture.full.document3.document, 0, &fixture.full, reader);
}

static void finalElementRanges()
{
    Fixture f;
    init(&f);
    const float source[7] = {1.25f, -2.5f, 3.75f, 991, 4, 5, 6};
    memcpy(f.bytes, source, sizeof(source));
    f.accessor.count = 1;
    f.views[0].byteLength = 12;
    L_AssetAccessorReader reader;
    double values[3];
    require(read(f, &reader) == BL_OK &&
                l_assetAccessorElement(&reader, 0, values) == BL_OK &&
                values[0] == 1.25 && values[1] == -2.5 && values[2] == 3.75,
            "count1 element fits12-byte view even with stride16");
    f.accessor.count = 2;
    require(read(f, &reader) == BL_INVALID_ARGUMENT,
            "count2 still overruns12-byte view with stride16");
    f.views[0].byteLength = 28;
    require(read(f, &reader) == BL_OK &&
                l_assetAccessorElement(&reader, 1, values) == BL_OK &&
                values[0] == 4 && values[1] == 5 && values[2] == 6,
            "last element needs no trailing stride padding");
    f.views[0].byteLength = 27;
    require(read(f, &reader) == BL_INVALID_ARGUMENT,
            "last element truncation is rejected independently of stride");
}

static void sparseAndGuards()
{
    Fixture f;
    init(&f);
    const float base[12] = {1, 2, 3, 991, 4, 5, 6, 992, 7, 8, 9, 993};
    memcpy(f.bytes, base, sizeof(base));
    const float replacements[6] = {-0.0f, 12, 13, 17, 18, 19};
    memcpy(f.bytes + 80, replacements, sizeof(replacements));
    uint16_t sparseIndices[2] = {0, 2};
    memcpy(f.bytes + 66, sparseIndices, sizeof(sparseIndices));
    f.accessor.sparsePresent = true;
    f.inputs.sparse = {2, 1, {true, 2}, BL_ASSET_COMPONENT_U16, 2, {}};
    L_AssetAccessorReader reader;
    require(read(f, &reader) == BL_OK, "interleaved base with independent sparse offsets");
    double values[16];
    require(l_assetAccessorElement(&reader, 0, values) == BL_OK && signbit(values[0]) &&
                values[1] == 12 && values[2] == 13,
            "sparse overwrite preserves signed zero");
    require(l_assetAccessorElement(&reader, 1, values) == BL_OK && values[0] == 4 &&
                values[1] == 5 && values[2] == 6,
            "untouched interleaved base is exact and ignores padding");
    require(l_assetAccessorElement(&reader, 2, values) == BL_OK && values[0] == 17 &&
                values[1] == 18 && values[2] == 19, "last sparse replacement");
    f.accessor.bufferView.present = false;
    require(read(f, &reader) == BL_OK &&
                l_assetAccessorElement(&reader, 1, values) == BL_OK && values[0] == 0 &&
                values[1] == 0 && values[2] == 0, "missing base starts as typed zeros");
    f.inputs.hasByteOffset = true;
    require(read(f, &reader) == BL_INVALID_ARGUMENT, "absent base explicit offset0 invalid");
    f.inputs.hasByteOffset = false;
    f.accessor.byteOffset = SIZE_MAX;
    require(read(f, &reader) == BL_OK, "absent original offset storage ignored");
    f.inputs.sparse.count = 0;
    require(read(f, &reader) == BL_INVALID_ARGUMENT, "sparse count0");
    f.inputs.sparse.count = 4;
    require(read(f, &reader) == BL_INVALID_ARGUMENT, "sparse count exceeds total");
    f.inputs.sparse.count = 2;
    sparseIndices[1] = 0;
    memcpy(f.bytes + 66, sparseIndices, sizeof(sparseIndices));
    require(read(f, &reader) == BL_INVALID_ARGUMENT, "duplicate sparse index");
    sparseIndices[0] = 2;
    sparseIndices[1] = 1;
    memcpy(f.bytes + 66, sparseIndices, sizeof(sparseIndices));
    require(read(f, &reader) == BL_INVALID_ARGUMENT, "descending sparse index");
    sparseIndices[0] = 0;
    sparseIndices[1] = 3;
    memcpy(f.bytes + 66, sparseIndices, sizeof(sparseIndices));
    require(read(f, &reader) == BL_INVALID_ARGUMENT, "out-of-range sparse index");
    sparseIndices[1] = 2;
    memcpy(f.bytes + 66, sparseIndices, sizeof(sparseIndices));
    f.inputs.sparse.indicesComponentType = BL_ASSET_COMPONENT_I16;
    require(read(f, &reader) == BL_INVALID_ARGUMENT, "signed sparse index type");
    f.inputs.sparse.indicesComponentType = BL_ASSET_COMPONENT_U16;
    f.inputs.sparse.indicesByteOffset.value = 1;
    require(read(f, &reader) == BL_INVALID_ARGUMENT, "sparse index logical misalignment");
    f.inputs.sparse.indicesByteOffset.value = 2;
    f.views[1].byteLength = 5;
    require(read(f, &reader) == BL_INVALID_ARGUMENT, "sparse indices truncation");
    f.views[1].byteLength = 8;
    f.views[2].byteLength = 23;
    require(read(f, &reader) == BL_INVALID_ARGUMENT, "sparse values last-element truncation");
    f.views[2].byteLength = 32;
    f.views[2].byteStride = {true, 16};
    require(read(f, &reader) == BL_INVALID_ARGUMENT, "sparse values must be tightly packed");
    f.views[2].byteStride = {};
    f.viewInputs[1].target = {true, 34963};
    require(read(f, &reader) == BL_INVALID_ARGUMENT, "sparse view target forbidden");
    f.viewInputs[1].target = {};
    f.inputs.sparse.valuesByteOffset = {true, SIZE_MAX};
    require(read(f, &reader) == BL_INVALID_ARGUMENT, "sparse offset overflow before pointer");
    f.inputs.sparse.valuesByteOffset = {};
    f.accessor.sparsePresent = false;
    memset(&f.inputs.sparse, 0xff, sizeof(f.inputs.sparse));
    f.inputs.min = {(const double*)1, SIZE_MAX};
    f.inputs.max = {(const double*)1, SIZE_MAX};
    require(read(f, &reader) == BL_OK &&
                l_validateAssetAccessorBounds(&f.full.document3.document, &f.full) == BL_OK,
            "absent sparse/bounds payload is ignored, not dereferenced");
    f.accessor.count = 0;
    require(read(f, &reader) == BL_INVALID_ARGUMENT, "v4 count0 schema restriction");
    f.accessor.count = SIZE_MAX;
    require(read(f, &reader) == BL_INVALID_ARGUMENT, "count multiplication overflow");
}

static void matricesAndNormalization()
{
    for (auto shape : {BL_ASSET_MAT2, BL_ASSET_MAT3, BL_ASSET_MAT4})
    {
        for (auto type : {BL_ASSET_COMPONENT_U8, BL_ASSET_COMPONENT_U16})
        {
            Fixture f;
            init(&f);
            size_t n = shape == BL_ASSET_MAT2 ? 2 : shape == BL_ASSET_MAT3 ? 3 : 4;
            size_t width = type == BL_ASSET_COMPONENT_U8 ? 1 : 2;
            size_t column = (n * width + 3) & ~size_t(3);
            memset(f.bytes, 0xfe, sizeof(f.bytes));
            f.accessor.shape = shape;
            f.accessor.componentType = type;
            f.accessor.count = 1;
            f.views[0].byteStride = {};
            f.views[0].byteLength = n * column;
            for (size_t c = 0; c < n; ++c)
            {
                for (size_t r = 0; r < n; ++r)
                {
                    uint16_t value = static_cast<uint16_t>(c * n + r + 1);
                    memcpy(f.bytes + c * column + r * width, &value, width);
                }
            }
            L_AssetAccessorReader reader;
            double values[16];
            require(read(f, &reader) == BL_OK && reader.elementBytes == n * column &&
                        l_assetAccessorElement(&reader, 0, values) == BL_OK,
                    "typed padded matrix layout");
            for (size_t i = 0; i < n * n; ++i)
            {
                require(values[i] == i + 1, "padding sentinel is never a component");
            }
            f.accessor.bufferView.present = false;
            f.accessor.sparsePresent = true;
            f.inputs.sparse = {1, 1, {}, BL_ASSET_COMPONENT_U8, 2, {}};
            f.bytes[64] = 0;
            memcpy(f.bytes + 80, f.bytes, n * column);
            f.views[2].byteLength = n * column;
            require(read(f, &reader) == BL_OK &&
                        l_assetAccessorElement(&reader, 0, values) == BL_OK &&
                        values[n * n - 1] == n * n, "sparse matrix also uses column padding");
            f.views[2].byteLength -= 1;
            require(read(f, &reader) == BL_INVALID_ARGUMENT,
                    "last padded sparse matrix must fit full element");
        }
    }
    for (auto type : {BL_ASSET_COMPONENT_U8, BL_ASSET_COMPONENT_U16})
    {
        Fixture f;
        init(&f);
        f.accessor = {{true, 0}, 0, 1, type, BL_ASSET_VEC4, true, false};
        f.inputs.hasNormalized = true;
        f.views[0].byteStride = {};
        uint16_t integers[4] = {0, 1, 127, 255};
        if (type == BL_ASSET_COMPONENT_U16)
        {
            integers[2] = 32768;
            integers[3] = 65535;
            memcpy(f.bytes, integers, 8);
        }
        else
        {
            for (unsigned i = 0; i < 4; ++i)
            {
                f.bytes[i] = static_cast<uint8_t>(integers[i]);
            }
        }
        L_AssetAccessorReader reader;
        double values[4];
        require(read(f, &reader) == BL_OK &&
                    l_assetAccessorElement(&reader, 0, values) == BL_OK, "normalized component read");
        double reciprocal = type == BL_ASSET_COMPONENT_U8 ? 1.0 / 255 : 1.0 / 65535;
        for (unsigned i = 0; i < 4; ++i)
        {
            float expected = static_cast<float>(integers[i] * reciprocal);
            float actual = static_cast<float>(values[i]);
            require(!memcmp(&expected, &actual, sizeof(float)),
                    "Number reciprocal multiplication followed by Float32 rounding");
        }
    }
}

static void bounds()
{
    Fixture f;
    init(&f);
    f.views[0].byteStride = {};
    f.views[0].byteLength = 36;
    const float values[9] = {1.2f, 0, 0, 2, 3, 4, 5, 6, 7};
    memcpy(f.bytes, values, sizeof(values));
    f.inputs.hasMin = true;
    f.inputs.hasMax = true;
    f.inputs.min = {f.low, 3};
    f.inputs.max = {f.high, 3};
    f.low[0] = 1.2;
    f.high[0] = 5;
    f.high[1] = 6;
    f.high[2] = 7;
    require(l_validateAssetAccessorBounds(&f.full.document3.document, &f.full) == BL_OK,
            "JSON bounds round to original component type");
    f.accessor.sparsePresent = true;
    f.inputs.sparse = {1, 1, {}, BL_ASSET_COMPONENT_U8, 2, {}};
    f.bytes[64] = 2;
    const float overlay[3] = {9, 10, 11};
    memcpy(f.bytes + 80, overlay, sizeof(overlay));
    require(l_validateAssetAccessorBounds(&f.full.document3.document, &f.full) == BL_INVALID_ARGUMENT,
            "base bounds cannot hide sparse final bounds");
    f.high[0] = 9;
    f.high[1] = 10;
    f.high[2] = 11;
    require(l_validateAssetAccessorBounds(&f.full.document3.document, &f.full) == BL_OK,
            "final sparse bounds include every resolved element");
    f.accessor.sparsePresent = false;
    f.accessor.bufferView.present = false;
    require(l_validateAssetAccessorBounds(&f.full.document3.document, &f.full) == BL_OK,
            "implicit-zero accessor min/max value exception");
    f.inputs.min.count = 2;
    require(l_validateAssetAccessorBounds(&f.full.document3.document, &f.full) == BL_INVALID_ARGUMENT,
            "implicit-zero exception does not waive tuple shape");
    f.inputs.min.count = 3;
    f.inputs.hasNormalized = true;
    f.accessor.normalized = false;
    require(l_validateAssetAccessorBounds(&f.full.document3.document, &f.full) == BL_OK,
            "explicit normalized=false on FLOAT is legal");
    f.accessor.normalized = true;
    require(l_validateAssetAccessorBounds(&f.full.document3.document, &f.full) == BL_INVALID_ARGUMENT,
            "normalized=true on FLOAT is invalid");
}

struct Memory
{
    size_t live = 0;
    bool fail = false;
};

static void* allocate(void* user, size_t bytes, size_t alignment)
{
    auto* memory = static_cast<Memory*>(user);
    void* result = memory->fail ? nullptr : _aligned_malloc(bytes, alignment);
    memory->live += result != nullptr;
    return result;
}

static void release(void* user, void* pointer, size_t, size_t)
{
    if (pointer)
    {
        --static_cast<Memory*>(user)->live;
        _aligned_free(pointer);
    }
}

static void topology()
{
    Memory memory;
    bl_RuntimeOptions options = {};
    options.allocator = {&memory, allocate, release};
    bl_Runtime* runtime = nullptr;
    require(bl_createRuntime(&options, &runtime) == BL_OK, "numeric runtime");
    Fixture f;
    init(&f);
    bl_AssetPrimitive primitive = {};
    primitive.hasMode = true;
    const uint32_t strip[9] = {0, 1, 2, 1, 3, 2, 2, 3, 4};
    const uint32_t fan[9] = {1, 2, 0, 2, 3, 0, 3, 4, 0};
    for (auto mode : {BL_ASSET_MODE_TRIANGLE_STRIP, BL_ASSET_MODE_TRIANGLE_FAN})
    {
        primitive.mode = mode;
        bl_U32Span indices = {};
        require(l_assetTriangleIndices(runtime, &f.full.document3.document, &primitive, 5,
                                        &f.full, &indices) == BL_OK &&
                    indices.count == 9 &&
                    !memcmp(indices.data, mode == BL_ASSET_MODE_TRIANGLE_STRIP ? strip : fan, 36),
                "normative nonindexed strip/fan winding");
        l_free(runtime, (void*)indices.data);
        for (size_t count = 0; count < 3; ++count)
        {
            require(l_assetTriangleIndices(runtime, &f.full.document3.document, &primitive,
                                            count, &f.full, &indices) == BL_INVALID_ARGUMENT,
                    "short strips/fans rejected before underflow");
        }
        memory.fail = true;
        indices = {nullptr, SIZE_MAX};
        require(l_assetTriangleIndices(runtime, &f.full.document3.document, &primitive, 5,
                                        &f.full, &indices) == BL_OUT_OF_MEMORY &&
                    indices.count == SIZE_MAX, "expansion OOM output unchanged");
        memory.fail = false;
    }
    f.accessor = {{true, 0}, 0, 5, BL_ASSET_COMPONENT_U8, BL_ASSET_SCALAR, false, false};
    f.views[0].byteStride = {};
    const uint8_t connector[5] = {0, 1, 1, 2, 3};
    memcpy(f.bytes, connector, sizeof(connector));
    primitive.indices = {true, 0};
    primitive.mode = BL_ASSET_MODE_TRIANGLE_STRIP;
    bl_U32Span indices = {};
    const uint32_t expected[9] = {0, 1, 1, 1, 2, 1, 1, 2, 3};
    require(l_assetTriangleIndices(runtime, &f.full.document3.document, &primitive, 4,
                                    &f.full, &indices) == BL_OK &&
                !memcmp(indices.data, expected, sizeof(expected)),
            "degenerates retained and strip parity advances");
    l_free(runtime, (void*)indices.data);
    f.bytes[4] = 255;
    require(l_assetTriangleIndices(runtime, &f.full.document3.document, &primitive, 256,
                                    &f.full, &indices) == BL_INVALID_ARGUMENT,
            "reserved restart value is not a glTF index");
    require(bl_disposeRuntime(runtime) == BL_OK && !memory.live, "numeric allocator teardown0");
}

int main()
{
    finalElementRanges();
    sparseAndGuards();
    matricesAndNormalization();
    bounds();
    topology();
    printf("Independent accessor/topology numeric %u checks passed; teardown0\n", checks);
}
