#include "AssetMotionInternal.h"
#include <babylon_lite_asset_decoder.h>
#include <json.hpp>
#include <filesystem>
#include <fstream>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

static size_t assertions;

static void require(bool condition, const char* message)
{
    ++assertions;
    if (!condition)
    {
        fprintf(stderr, "Skin animation CPU: %s\n", message);
        exit(2);
    }
}

struct Memory
{
    size_t calls;
    size_t failAt;
    size_t live;
};

static void* allocate(void* user, size_t bytes, size_t alignment)
{
    Memory* memory = (Memory*)user;
    if (++memory->calls == memory->failAt)
    {
        return NULL;
    }
    void* pointer = _aligned_malloc(bytes, alignment);
    memory->live += pointer != NULL;
    return pointer;
}

static void deallocate(void* user, void* pointer, size_t, size_t)
{
    if (pointer)
    {
        --((Memory*)user)->live;
        _aligned_free(pointer);
    }
}

static void numeric()
{
    float times[] = {1, 3};
    float values[] = {2, 4, 6, 6, 8, 10};
    L_AssetSampler sampler = {times, values, 2, 3, 0};
    float out[4] = {};
    require(l_sampleAssetAnimation(&sampler, 0, false, out) == BL_OK && out[0] == 2,
            "timeline begins0 before first key");
    require(l_sampleAssetAnimation(&sampler, 2, false, out) == BL_OK && out[0] == 4,
            "independent linear midpoint");
    sampler.interpolation = 1;
    require(l_sampleAssetAnimation(&sampler, 3, false, out) == BL_OK && out[0] == 6,
            "STEP exact last key");
    times[0] = 0;
    times[1] = 2;
    float cubic[] = {0, 0, 0, 1, 1, 1, 0, 1, 0,
                     0, 0, 0, 1, 2, 1, 0, 0, 0};
    sampler.values = cubic;
    sampler.interpolation = 2;
    require(l_sampleAssetAnimation(&sampler, 1, false, out) == BL_OK && out[1] == 1.75f,
            "independent Hermite derivative dt multiplication");
    float quaternion[] = {0, 0, 0, 1, 0, 0, 0, -1};
    sampler = {times, quaternion, 2, 4, 0};
    require(l_sampleAssetAnimation(&sampler, 1, true, out) == BL_OK && out[3] == 1,
            "quaternion shortest hemisphere");
    quaternion[6] = -1;
    quaternion[7] = 0;
    require(l_sampleAssetAnimation(&sampler, 1, true, out) == BL_OK &&
                fabs(out[2] + sqrt(.5)) < 2e-6 && fabs(out[3] - sqrt(.5)) < 2e-6,
            "independent quaternion90-degree midpoint");
}

int main(int argc, char** argv)
{
    require(argc == 3, "fixture directory and receipt required");
    numeric();
    Memory memory = {};
    bl_Allocator allocator = {&memory, allocate, deallocate};
    bl_AssetDecoder* decoder = NULL;
    bl_AssetService service = {};
    require(bl_createAssetDecoder(&allocator, &decoder, &service) == BL_OK && service.version == 2,
            "legacy factory frozen");
    require(bl_assetDecoderService6(decoder, &service) == BL_OK && service.version == 6,
            "explicit raw6 factory");
    struct Guard
    {
        bl_ParsedGltf5 old;
        unsigned char canary[sizeof(bl_ParsedGltf6)];
    };
    Guard guard = {};
    guard.old.document4.document3.document = {sizeof(bl_ParsedGltf5), 5};
    memset(guard.canary, 0x73, sizeof(guard.canary));
    bl_ParsedGltf* prefix = &guard.old.document4.document3.document;
    require(service.parseGltf(service.userData, {}, {}, prefix) == BL_INVALID_ARGUMENT,
            "short old storage rejected");
    service.releaseGltf(service.userData, prefix);
    for (unsigned char value : guard.canary)
    {
        require(value == 0x73, "no tail write on short storage or release");
    }
    using Json = nlohmann::json;
    std::filesystem::path directory(argv[1]);
    Json manifest = Json::parse(std::ifstream(directory / "fixtures.json"));
    size_t cases = 0;
    for (const auto& fixture : manifest["fixtures"])
    {
        std::ifstream input(directory / fixture["file"].get<std::string>(), std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
        require(!bytes.empty(), "actual selected fixture bytes");
        bl_ParsedGltf6 parsed = {};
        prefix = &parsed.document5.document4.document3.document;
        prefix->structSize = sizeof(parsed);
        prefix->version = 6;
        bl_Status status =
            service.parseGltf(service.userData, {bytes.data(), bytes.size()}, {}, prefix);
        const char* reason = NULL;
        if (status == BL_OK)
        {
            status = l_validateAsset(prefix, &reason, &parsed.document5.document4.document3,
                                     &parsed.document5.document4, &parsed.document5, &parsed);
        }
        std::string expected = fixture["expectedStatus"];
        bl_Status wanted = expected == "BL_OK" ? BL_OK
                           : expected == "BL_UNSUPPORTED" ? BL_UNSUPPORTED
                                                          : BL_INVALID_ARGUMENT;
        if (status != wanted)
        {
            fprintf(stderr, "%s: got%d expected%d reason=%s\n",
                    fixture["id"].get<std::string>().c_str(), status, wanted,
                    reason ? reason : "motion/schema");
        }
        require(status == wanted, "independently labeled raw6 validation");
        prefix->structSize = sizeof(bl_ParsedGltf);
        prefix->version = 2;
        service.releaseGltf(service.userData, prefix);
        require(memory.live == 1, "all partial result allocations released by allocation token");
        ++cases;
    }
    require(cases >= 61, "no zero-selected passes");
    std::ifstream faultInput(directory / "skin-four-clips.glb", std::ios::binary);
    std::vector<uint8_t> faultBytes((std::istreambuf_iterator<char>(faultInput)), {});
    size_t before = memory.calls;
    bl_ParsedGltf6 baseline = {};
    baseline.document5.document4.document3.document = {sizeof(baseline), 6};
    prefix = &baseline.document5.document4.document3.document;
    require(service.parseGltf(service.userData, {faultBytes.data(), faultBytes.size()}, {}, prefix) ==
                BL_OK, "provider allocation baseline");
    size_t allocations = memory.calls - before;
    service.releaseGltf(service.userData, prefix);
    for (size_t fail = 1; fail <= allocations; ++fail)
    {
        bl_ParsedGltf6 partial = {};
        partial.document5.document4.document3.document = {sizeof(partial), 6};
        prefix = &partial.document5.document4.document3.document;
        memory.failAt = memory.calls + fail;
        require(service.parseGltf(service.userData, {faultBytes.data(), faultBytes.size()}, {},
                                  prefix) == BL_OUT_OF_MEMORY, "every provider allocation OOM");
        service.releaseGltf(service.userData, prefix);
        memory.failAt = 0;
        require(memory.live == 1, "every partial descriptor release once");
    }
    bl_destroyAssetDecoder(decoder);
    require(memory.live == 0, "decoder teardown0");
    Json receipt = {{"cases", cases}, {"assertions", assertions}, {"providerFailpoints", allocations},
                    {"allocatorLive", memory.live}, {"state", "PASSED"}};
    std::ofstream(argv[2]) << receipt.dump(2);
    printf("Skin animation CPU: %zu cases, %zu assertions\n", cases, assertions);
}
