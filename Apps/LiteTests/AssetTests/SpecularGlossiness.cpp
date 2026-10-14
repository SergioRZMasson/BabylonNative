#include "AssetInternal.h"
#include "PbrInternal.h"
#include <babylon_lite_asset_decoder.h>
#include <json.hpp>
#include <filesystem>
#include <fstream>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

using Json = nlohmann::json;

static size_t assertions = 0;

static void require(bool value, const char* message)
{
    ++assertions;
    if (!value)
    {
        fprintf(stderr, "SpecGloss CPU: %s\n", message);
        exit(2);
    }
}

struct Memory
{
    size_t calls = 0;
    size_t failAt = 0;
    size_t live = 0;
};

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

static void deallocate(void* user, void* pointer, size_t, size_t)
{
    if (pointer)
    {
        --static_cast<Memory*>(user)->live;
        _aligned_free(pointer);
    }
}

static bl_ParsedGltf5 result()
{
    bl_ParsedGltf5 parsed = {};
    parsed.document4.document3.document.structSize = sizeof(parsed);
    parsed.document4.document3.document.version = BL_ASSET_DOCUMENT_VERSION_5;
    return parsed;
}

static std::vector<uint8_t> file(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "synthetic fixture exists");
    return {std::istreambuf_iterator<char>(input), {}};
}

static std::vector<uint8_t> noBin(const Json& document)
{
    std::string text = document.dump();
    text.append((4 - text.size() % 4) % 4, ' ');
    std::vector<uint8_t> bytes(20 + text.size());
    uint32_t header[5] = {0x46546c67, 2, static_cast<uint32_t>(bytes.size()),
                          static_cast<uint32_t>(text.size()), 0x4e4f534a};
    memcpy(bytes.data(), header, sizeof(header));
    memcpy(bytes.data() + 20, text.data(), text.size());
    return bytes;
}

static bl_Status validate(const bl_ParsedGltf5& parsed)
{
    return l_validateAsset(&parsed.document4.document3.document, nullptr,
                           &parsed.document4.document3, &parsed.document4, &parsed);
}

static bl_Status expected(const std::string& name)
{
    return name == "BL_OK" ? BL_OK
           : name == "BL_INVALID_ARGUMENT" ? BL_INVALID_ARGUMENT
                                          : BL_UNSUPPORTED;
}

static void presence(bl_AssetService service, Memory& memory)
{
    auto bytes = noBin({{"asset", {{"version", "2.0"}}},
                       {"extensionsUsed", {"KHR_materials_pbrSpecularGlossiness"}},
                       {"materials", {{{"extensions", {{"KHR_materials_pbrSpecularGlossiness",
                           {{"diffuseFactor", {.25, .5, .75, .7312345678901234}},
                            {"specularFactor", {.125, .375, .625}},
                            {"glossinessFactor", .6875}}}}}}, Json::object()}},
                       {"scenes", {{{"nodes", Json::array()}}}}});
    auto parsed = result();
    auto* prefix = &parsed.document4.document3.document;
    require(service.parseGltf(service.userData, {bytes.data(), bytes.size()}, {}, prefix) == BL_OK,
            "service5 preserves valid noBIN/zero-buffer unused materials");
    auto input = parsed.specularGlossinessInputs[0];
    require(input.present && input.hasDiffuseFactor && input.hasSpecularFactor &&
                input.hasGlossinessFactor && input.diffuseFactor.a == .7312345678901234 &&
                input.specularFactor.y == .375 && input.glossinessFactor == .6875 &&
                !input.diffuse.present && !input.specularGlossiness.present &&
                !parsed.specularGlossinessInputs[1].present && !prefix->bufferCount,
            "raw presence, exact source doubles, dense numbering and absent maps");
    bytes.assign(bytes.size(), 0);
    require(validate(parsed) == BL_OK, "copied source survives call-only input lifetime");
    auto* supplements = const_cast<bl_AssetSpecularGlossinessInputs*>(
        parsed.specularGlossinessInputs);
    supplements[1].hasDiffuseFactor = true;
    supplements[1].diffuseFactor.a = NAN;
    supplements[1].diffuse.present = true;
    supplements[1].diffuse.index = UINT32_MAX;
    require(validate(parsed) == BL_OK, "absent extension ignores all nonapplicable storage");
    supplements[0].hasGlossinessFactor = false;
    supplements[0].glossinessFactor = NAN;
    supplements[0].specularGlossiness.index = UINT32_MAX;
    supplements[0].specularGlossiness.unsupportedFeatures = UINT64_MAX;
    require(validate(parsed) == BL_OK, "absent factor/map payloads ignored");
    supplements[0].hasGlossinessFactor = true;
    require(validate(parsed) == BL_INVALID_ARGUMENT, "present nonfinite factor rejected");
    supplements[0].glossinessFactor = .5;
    parsed.specularGlossinessInputs = nullptr;
    require(validate(parsed) == BL_INVALID_ARGUMENT, "dense supplement pointer required");
    parsed.specularGlossinessInputs = supplements;
    prefix->structSize = sizeof(bl_ParsedGltf);
    prefix->version = 2;
    service.releaseGltf(service.userData, prefix);
    require(memory.live == 1, "corrupted prefix frees entire chain, no tail clearing");
}

static void shortStorage(bl_AssetService service, Memory& memory)
{
    struct Guarded
    {
        bl_ParsedGltf value;
        uint8_t canary[sizeof(bl_ParsedGltf5)];
    };
    Guarded guarded = {};
    guarded.value.structSize = sizeof(guarded.value);
    guarded.value.version = 2;
    memset(guarded.canary, 0x79, sizeof(guarded.canary));
    auto bytes = noBin({{"asset", {{"version", "2.0"}}}});
    require(service.parseGltf(service.userData, {bytes.data(), bytes.size()}, {},
                              &guarded.value) == BL_INVALID_ARGUMENT,
            "service5 rejects old short storage before tail access");
    service.releaseGltf(service.userData, &guarded.value);
    for (auto byte : guarded.canary)
    {
        require(byte == 0x79, "short-storage release keeps tail canary unchanged");
    }
    auto parsed = result();
    parsed.document4.document3.document.version = 4;
    require(service.parseGltf(service.userData, {bytes.data(), bytes.size()}, {},
                              &parsed.document4.document3.document) == BL_INVALID_ARGUMENT,
            "service5 rejects mismatched version");
    service.releaseGltf(service.userData, &parsed.document4.document3.document);
    require(memory.live == 1, "guard failures allocate no provider storage");
}

static void getter(Memory& memory)
{
    bl_RuntimeOptions options = {};
    options.allocator = {&memory, allocate, deallocate};
    bl_Runtime* runtime = nullptr;
    require(bl_createRuntime(&options, &runtime) == BL_OK, "CPU runtime");
    L_Record* record = nullptr;
    require(l_record(runtime, sizeof(L_Material), L_PBR, 20, nullptr, &record) == BL_OK,
            "private CPU material record");
    auto* material = reinterpret_cast<L_Material*>(record);
    material->pbrSpecularGlossiness = true;
    bl_PbrMaterial handle = {runtime, record->id};
    bl_PbrMaterialInfo out = {};
    memset(&out, 0x63, sizeof(out));
    auto before = out;
    require(bl_getPbrMaterialInfo(handle, &out) == BL_UNSUPPORTED &&
                !memcmp(&before, &out, sizeof(out)),
            "frozen MR getter explicitly unsupported for SG, output unchanged");
    bl_Error error = {};
    require(bl_getLastError(runtime, &error) == BL_OK && error.status == BL_UNSUPPORTED &&
                error.message.length > 0,
            "standard explicit workflow-record diagnostic");
    bl_Material generic = {};
    bl_PbrMaterial converted = {};
    require(bl_pbrMaterialAsMaterial(handle, &generic) == BL_OK &&
                bl_materialAsPbrMaterial(generic, &converted) == BL_OK &&
                converted._id == handle._id, "SG generic conversions remain supported");
    material->pbrSpecularGlossiness = false;
    material->pbrInfo.roughnessFactor = .375;
    require(bl_getPbrMaterialInfo(handle, &out) == BL_OK && out.roughnessFactor == .375,
            "unchanged MR getter");
    require(bl_disposeRuntime(runtime) == BL_OK, "CPU runtime teardown");
}

int main(int argc, char** argv)
{
    require(argc == 3, "synthetic directory and ignored result path required");
    std::filesystem::path fixtures(argv[1]);
    Memory memory;
    bl_Allocator allocator = {&memory, allocate, deallocate};
    bl_AssetDecoder* decoder = nullptr;
    bl_AssetService old = {};
    require(bl_createAssetDecoder(&allocator, &decoder, &old) == BL_OK && old.version == 2,
            "old factory default frozen");
    bl_AssetService third = {};
    bl_AssetService fourth = {};
    bl_AssetService fifth = {};
    require(bl_assetDecoderService3(decoder, &third) == BL_OK && third.version == 3 &&
                bl_assetDecoderService4(decoder, &fourth) == BL_OK && fourth.version == 4 &&
                bl_assetDecoderService5(decoder, &fifth) == BL_OK && fifth.version == 5,
            "explicit independent protocol factories");
    presence(fifth, memory);
    shortStorage(fifth, memory);
    auto manifest = Json::parse(std::ifstream(fixtures / "fixture-manifest.json"));
    size_t passed = 0;
    for (const auto& fixture : manifest["fixtures"])
    {
        auto bytes = file(fixtures / fixture["file"].get<std::string>());
        auto parsed = result();
        auto* prefix = &parsed.document4.document3.document;
        auto status = fifth.parseGltf(fifth.userData, {bytes.data(), bytes.size()}, {}, prefix);
        require(status == expected(fixture["expectedProvider5"].get<std::string>()),
                "exact synthetic schema result");
        if (status == BL_OK)
        {
            require(validate(parsed) == expected(fixture["expectedCore5"].get<std::string>()),
                    "exact synthetic Core factor/type/reference/UV/extension result");
        }
        fifth.releaseGltf(fifth.userData, prefix);
        require(memory.live == 1, "every partial/schema/valid provider result released once");
        ++passed;
    }
    auto bytes = file(fixtures / "mapped-factors.glb");
    size_t faults = 0;
    for (size_t fault = 1; ; ++fault)
    {
        memory.failAt = memory.calls + fault;
        auto parsed = result();
        auto* prefix = &parsed.document4.document3.document;
        auto status = fifth.parseGltf(fifth.userData, {bytes.data(), bytes.size()}, {}, prefix);
        fifth.releaseGltf(fifth.userData, prefix);
        require(memory.live == 1 && (status == BL_OK || status == BL_OUT_OF_MEMORY),
                "all provider allocation positions partial-release balanced");
        if (status == BL_OK)
        {
            break;
        }
        ++faults;
    }
    memory.failAt = 0;
    getter(memory);
    bl_destroyAssetDecoder(decoder);
    require(!memory.live, "tracked CPU/provider teardown0");
    std::ofstream(argv[2]) << Json({{"assertions", assertions}, {"syntheticCases", passed},
        {"providerOomPositions", faults},
        {"trackedTeardown", 0}}).dump(2) << '\n';
    printf("SpecGloss CPU %zu cases, %zu allocation faults, %zu assertions, teardown0\n",
           passed, faults, assertions);
}
