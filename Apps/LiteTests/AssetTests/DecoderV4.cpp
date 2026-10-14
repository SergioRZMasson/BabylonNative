#include <babylon_lite_asset_decoder.h>
#include <cstring>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

struct Memory
{
    size_t live = 0;
    size_t calls = 0;
    size_t failAt = 0;
};

static void require(bool value, const char* message)
{
    if (!value)
    {
        fprintf(stderr, "Raw decoder4: %s\n", message);
        exit(2);
    }
}

static void* allocate(void* user, size_t bytes, size_t alignment)
{
    auto* memory = static_cast<Memory*>(user);
    if (++memory->calls == memory->failAt)
    {
        return nullptr;
    }
    void* result = _aligned_malloc(bytes, alignment);
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

static std::vector<uint8_t> glb(std::string json)
{
    while (json.size() % 4)
    {
        json += ' ';
    }
    std::vector<uint8_t> bytes(20 + json.size() + 8 + 64, 0x39);
    uint32_t header[5] = {UINT32_C(0x46546c67), 2, static_cast<uint32_t>(bytes.size()),
                          static_cast<uint32_t>(json.size()), UINT32_C(0x4e4f534a)};
    memcpy(bytes.data(), header, sizeof(header));
    memcpy(bytes.data() + 20, json.data(), json.size());
    uint32_t bin[2] = {64, UINT32_C(0x004e4942)};
    memcpy(bytes.data() + 20 + json.size(), bin, sizeof(bin));
    return bytes;
}

static bl_ParsedGltf4 result()
{
    bl_ParsedGltf4 value = {};
    value.document3.document.structSize = sizeof(value);
    value.document3.document.version = BL_ASSET_DOCUMENT_VERSION_4;
    return value;
}

int main()
{
    const std::string source =
        R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":64}],)"
        R"("bufferViews":[{"buffer":0,"byteLength":32,"target":34962},)"
        R"({"buffer":0,"byteOffset":32,"byteLength":4},)"
        R"({"buffer":0,"byteOffset":36,"byteLength":12}],)"
        R"("accessors":[{"bufferView":0,"componentType":5126,"normalized":false,)"
        R"("count":2,"type":"VEC3","min":[0.1234567890123456,0,0],"max":[1,1,1],)"
        R"("sparse":{"count":1,"indices":{"bufferView":1,"componentType":5121},)"
        R"("values":{"bufferView":2,"byteOffset":0}}},)"
        R"({"componentType":5125,"normalized":false,"count":1,"type":"SCALAR"}]})";
    auto bytes = glb(source);
    Memory memory;
    bl_Allocator allocator = {&memory, allocate, release};
    bl_AssetDecoder* decoder = nullptr;
    bl_AssetService old = {};
    require(bl_createAssetDecoder(&allocator, &decoder, &old) == BL_OK && old.version == 2,
            "legacy default factory unchanged");
    bl_AssetService third = {};
    bl_AssetService fourth = {};
    require(bl_assetDecoderService3(decoder, &third) == BL_OK && third.version == 3 &&
                bl_assetDecoderService4(decoder, &fourth) == BL_OK && fourth.version == 4,
            "explicit factories advertise independent versions");
    auto parsed = result();
    auto* prefix = &parsed.document3.document;
    require(fourth.parseGltf(fourth.userData, {bytes.data(), bytes.size()}, {}, prefix) == BL_OK,
            "full nested v4 raw document");
    const auto& accessor = prefix->accessors[0];
    const auto& inputs = parsed.accessorInputs[0];
    require(accessor.sparsePresent && !inputs.hasByteOffset && inputs.hasNormalized &&
                !accessor.normalized && inputs.hasMin && inputs.hasMax &&
                inputs.min.data[0] == 0.1234567890123456 && inputs.sparse.count == 1 &&
                !inputs.sparse.indicesByteOffset.present &&
                inputs.sparse.valuesByteOffset.present &&
                inputs.sparse.valuesByteOffset.value == 0 &&
                inputs.sparse.indicesComponentType == BL_ASSET_COMPONENT_U8,
            "source sparse/presence/double fields retained without conversion");
    require(!parsed.bufferViewInputs[0].hasByteOffset &&
                parsed.bufferViewInputs[0].target.present &&
                parsed.bufferViewInputs[0].target.value == 34962 &&
                parsed.bufferViewInputs[1].hasByteOffset &&
                !parsed.bufferViewInputs[1].target.present &&
                prefix->bufferViewCount == 3 && prefix->accessorCount == 2,
            "raw view target/presence and original numbering preserved");
    bytes.assign(bytes.size(), 0);
    require(prefix->buffers[0].bytes.data[0] == 0x39 &&
                prefix->accessors[1].componentType == BL_ASSET_COMPONENT_U32 &&
                !prefix->accessors[1].normalized && parsed.accessorInputs[1].hasNormalized,
            "input call-only lifetime; explicit normalized=false on U32 allowed");
    prefix->structSize = sizeof(bl_ParsedGltf);
    prefix->version = 2;
    fourth.releaseGltf(fourth.userData, prefix);
    require(memory.live == 1, "corrupted returned prefix still frees complete allocation chain");
    struct Guarded
    {
        bl_ParsedGltf shortResult;
        uint8_t canary[sizeof(bl_ParsedGltf4)];
    };
    Guarded shortResult = {};
    memset(shortResult.canary, 0x75, sizeof(shortResult.canary));
    shortResult.shortResult.structSize = sizeof(bl_ParsedGltf);
    shortResult.shortResult.version = 2;
    bytes = glb(source);
    require(fourth.parseGltf(fourth.userData, {bytes.data(), bytes.size()}, {},
                               &shortResult.shortResult) == BL_INVALID_ARGUMENT,
            "short storage rejected before trailing access");
    fourth.releaseGltf(fourth.userData, &shortResult.shortResult);
    for (uint8_t byte : shortResult.canary)
    {
        require(byte == 0x75, "short-storage release never clears a v4 tail");
    }
    unsigned faults = 0;
    for (size_t fault = 1; fault < 128; ++fault)
    {
        memory.failAt = memory.calls + fault;
        parsed = result();
        auto status = fourth.parseGltf(fourth.userData, {bytes.data(), bytes.size()}, {},
                                       &parsed.document3.document);
        fourth.releaseGltf(fourth.userData, &parsed.document3.document);
        require(memory.live == 1 && (status == BL_OK || status == BL_OUT_OF_MEMORY),
                "every partial provider allocation is releasable exactly once");
        if (status == BL_OK)
        {
            break;
        }
        ++faults;
    }
    memory.failAt = 0;
    for (const char* malformed : {
             R"({"count":0,"indices":{},"values":{}})",
             R"({"count":1,"indices":{"bufferView":0},"values":{"bufferView":1}})",
             "null", "[]"})
    {
        std::string json =
            R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":64}],"accessors":[)"
            R"({"componentType":5126,"count":1,"type":"VEC3","sparse":)" +
            std::string(malformed) + "}]}";
        auto bad = glb(json);
        parsed = result();
        require(fourth.parseGltf(fourth.userData, {bad.data(), bad.size()}, {},
                                   &parsed.document3.document) == BL_INVALID_ARGUMENT,
                "malformed sparse schema is not ignored");
        fourth.releaseGltf(fourth.userData, &parsed.document3.document);
    }
    bl_ParsedEnvironment environment = {};
    environment.structSize = sizeof(environment);
    environment.version = 2;
    require(fourth.parseEnvironment(fourth.userData, {}, &environment) == BL_INVALID_ARGUMENT,
            "environment retains v2 contract");
    fourth.releaseEnvironment(fourth.userData, &environment);
    bl_destroyAssetDecoder(decoder);
    require(!memory.live && faults > 10, "complete raw decoder teardown");
    printf("Decoder4 nested-storage/presence/lifetime/schema and %u OOM positions passed\n", faults);
}
