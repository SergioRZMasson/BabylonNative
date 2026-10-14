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
        fprintf(stderr, "Raw decoder3: %s\n", message);
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
    if (result)
    {
        ++memory->live;
    }
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
    std::vector<uint8_t> bytes(20 + json.size() + 8 + 16);
    uint32_t header[5] = {UINT32_C(0x46546c67), 2, static_cast<uint32_t>(bytes.size()),
                          static_cast<uint32_t>(json.size()), UINT32_C(0x4e4f534a)};
    memcpy(bytes.data(), header, sizeof(header));
    memcpy(bytes.data() + 20, json.data(), json.size());
    uint32_t binary[2] = {16, UINT32_C(0x004e4942)};
    memcpy(bytes.data() + 20 + json.size(), binary, sizeof(binary));
    return bytes;
}

static bl_ParsedGltf3 result()
{
    bl_ParsedGltf3 value = {};
    value.document.structSize = sizeof(value);
    value.document.version = BL_ASSET_DOCUMENT_VERSION_3;
    return value;
}

int main()
{
    const std::string source =
        R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":16}],)"
        R"("bufferViews":[{"buffer":0,"byteLength":16}],)"
        R"("images":[{"bufferView":0,"mimeType":"image/png"}],)"
        R"("textures":[{"source":0,"sampler":0}],)"
        R"("samplers":[{},{"wrapS":10497,"wrapT":33071,"minFilter":9985,"magFilter":9728}],)"
        R"("materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0},)"
        R"("baseColorFactor":[0.1234567890123456,1,1,1],"metallicRoughnessTexture":{"index":0,"texCoord":1}},)"
        R"("normalTexture":{"index":0,"scale":0},"occlusionTexture":{"index":0,"strength":0},)"
        R"("emissiveTexture":{"index":0}}]})";
    auto bytes = glb(source);
    Memory memory;
    bl_Allocator allocator = {&memory, allocate, release};
    bl_AssetDecoder* decoder = nullptr;
    bl_AssetService legacy = {};
    require(bl_createAssetDecoder(&allocator, &decoder, &legacy) == BL_OK &&
                legacy.version == BL_ASSET_SERVICE_VERSION, "old service2 factory remains unchanged");
    bl_AssetService current = {};
    require(bl_assetDecoderService3(decoder, &current) == BL_OK &&
                current.version == BL_ASSET_SERVICE_VERSION_3, "explicit v3 capability");
    bl_ParsedGltf old = {};
    old.structSize = sizeof(old);
    old.version = BL_ASSET_DOCUMENT_VERSION;
    require(legacy.parseGltf(legacy.userData, {bytes.data(), bytes.size()}, {}, &old) == BL_OK &&
                old.imageCount == 1 && old.materialCount == 1, "legacy raw counts protocol still works");
    legacy.releaseGltf(legacy.userData, &old);
    auto parsed = result();
    require(current.parseGltf(current.userData, {bytes.data(), bytes.size()}, {},
                               &parsed.document) == BL_OK, "raw v3 encoded image descriptors");
    require(parsed.document.imageCount == 1 && parsed.images[0].bufferView.present &&
                parsed.images[0].hasMimeType && !parsed.images[0].hasUri,
            "image source presence is preserved");
    require(!parsed.samplers[0].wrapS.present && parsed.samplers[1].wrapS.present &&
                parsed.samplers[1].wrapS.value == 10497,
            "missing versus explicit default sampler is not collapsed");
    require(!parsed.materialInputs[0].baseColor.texCoord.present &&
                parsed.materialInputs[0].metallicRoughness.texCoord.present &&
                parsed.materialInputs[0].metallicRoughness.texCoord.value == 1 &&
                parsed.materialInputs[0].normal.hasScale &&
                parsed.materialInputs[0].normal.scale == 0 &&
                parsed.materialInputs[0].occlusion.hasStrength &&
                parsed.materialInputs[0].occlusion.strength == 0,
            "texCoord/normal scale/strength missing and zero values remain distinct");
    require(parsed.document.materials[0].hasBaseColorFactor &&
                parsed.document.materials[0].baseColorFactor.r == 0.1234567890123456,
            "source double factor precision retained");
    require(parsed.materialInputs[0].normal.present && parsed.materialInputs[0].emissive.present &&
                parsed.document.materials[0].textureInfoFields == 31,
            "unimplemented image slots returned as raw data rather than ignored");
    current.releaseGltf(current.userData, &parsed.document);
    require(memory.live == 1, "all raw document arrays/result tokens released");
    bl_ParsedGltf wrong = {};
    wrong.structSize = sizeof(wrong);
    wrong.version = BL_ASSET_DOCUMENT_VERSION;
    require(current.parseGltf(current.userData, {bytes.data(), bytes.size()}, {}, &wrong) ==
                BL_INVALID_ARGUMENT, "real version2 storage rejected before v3 tail access");
    current.releaseGltf(current.userData, &wrong);
    unsigned oomPositions = 0;
    for (size_t fault = 1; fault < 128; ++fault)
    {
        memory.failAt = memory.calls + fault;
        auto out = result();
        auto status = current.parseGltf(current.userData, {bytes.data(), bytes.size()}, {}, &out.document);
        current.releaseGltf(current.userData, &out.document);
        require(memory.live == 1 && (status == BL_OK || status == BL_OUT_OF_MEMORY),
                "raw v3 partial allocation failures are releasable once");
        if (status == BL_OK)
        {
            break;
        }
        ++oomPositions;
    }
    memory.failAt = 0;
    auto bad = result();
    require(current.parseGltf(current.userData, {bytes.data(), 7}, {}, &bad.document) ==
                BL_INVALID_ARGUMENT, "truncated GLB header");
    current.releaseGltf(current.userData, &bad.document);
    bl_destroyAssetDecoder(decoder);
    require(!memory.live, "final provider bytes released");
    printf("Decoder3 presence/double/legacy/full-prefix and %u allocation faults passed\n", oomPositions);
}
