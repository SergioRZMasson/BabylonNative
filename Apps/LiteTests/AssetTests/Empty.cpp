#include "AssetInternal.h"
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

static void require(bool condition, const char* message)
{
    ++assertions;
    if (!condition)
    {
        fprintf(stderr, "Empty scene CPU: %s\n", message);
        exit(2);
    }
}

struct State
{
    size_t live = 0;
    size_t calls = 0;
    size_t failAt = 0;
    size_t parses = 0;
    size_t releases = 0;
    size_t reads = 0;
    size_t readReleases = 0;
    bl_Status partialStatus = BL_OK;
    bool corrupt = false;
    bl_AssetDecoder* decoder = nullptr;
    bl_AssetService raw = {};
    const bl_ParsedGltf* prefix = nullptr;
    std::vector<uint8_t> input;
};

static void* allocate(void* user, size_t bytes, size_t alignment)
{
    auto* state = static_cast<State*>(user);
    if (++state->calls == state->failAt)
    {
        return nullptr;
    }
    void* result = _aligned_malloc(bytes, alignment);
    state->live += result != nullptr;
    return result;
}

static void release(void* user, void* pointer, size_t, size_t)
{
    if (pointer)
    {
        --static_cast<State*>(user)->live;
        _aligned_free(pointer);
    }
}

static bl_Status parse(void* user, bl_Bytes bytes, bl_String base, bl_ParsedGltf* out)
{
    auto* state = static_cast<State*>(user);
    state->prefix = out;
    ++state->parses;
    auto status = state->raw.parseGltf(state->raw.userData, bytes, base, out);
    if (state->corrupt)
    {
        out->version = 99;
    }
    return status == BL_OK ? state->partialStatus : status;
}

static void releaseParsed(void* user, bl_ParsedGltf* out)
{
    auto* state = static_cast<State*>(user);
    require(state->prefix == out, "same original result prefix released exactly once");
    ++state->releases;
    state->raw.releaseGltf(state->raw.userData, out);
}

static bl_Status read(void* user, bl_String, bl_HostBuffer* out)
{
    auto* state = static_cast<State*>(user);
    ++state->reads;
    out->data = state->input.data();
    out->byteCount = state->input.size();
    return BL_OK;
}

static void releaseRead(void* user, bl_HostBuffer* out)
{
    ++static_cast<State*>(user)->readReleases;
    *out = {};
}

static bl_Status wait(void*)
{
    return BL_OK;
}

static std::vector<uint8_t> file(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "independent fixture exists");
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(input), {});
}

static bl_EngineContext setup(State& state, uint32_t version)
{
    bl_Allocator allocator = {&state, allocate, release};
    require(bl_createAssetDecoder(&allocator, &state.decoder, &state.raw) == BL_OK,
            "decoder setup");
    if (version == 3)
    {
        require(bl_assetDecoderService3(state.decoder, &state.raw) == BL_OK, "service3 setup");
    }
    else if (version == 4)
    {
        require(bl_assetDecoderService4(state.decoder, &state.raw) == BL_OK, "service4 setup");
    }
    auto assets = state.raw;
    assets.userData = &state;
    assets.parseGltf = parse;
    assets.releaseGltf = releaseParsed;
    bl_IOService io = {};
    io.userData = &state;
    io.readFile = read;
    io.releaseBuffer = releaseRead;
    bl_RuntimeOptions2 options = {};
    options.structSize = sizeof(options);
    options.version = 2;
    options.base.allocator = allocator;
    options.base.io = &io;
    options.assets = &assets;
    bl_Runtime* runtime = nullptr;
    require(bl_createRuntime2(&options, &runtime) == BL_OK, "data-only runtime without bgfx init");
    L_Record* record = nullptr;
    require(l_record(runtime, sizeof(L_Engine), L_ENGINE, 1, nullptr, &record) == BL_OK,
            "data-only engine record, no GPU setup");
    auto* engine = reinterpret_cast<L_Engine*>(record);
    engine->runtime = runtime;
    engine->native.ownership = BL_BGFX_BORROWED;
    engine->native.waitForSubmittedWork = wait;
    return {runtime, record->id};
}

static bl_Status load(State& state, bl_EngineContext engine, bool uri, bl_AssetContainer* out)
{
    bl_AssetSource source =
        uri ? bl_AssetSource{BL_ASSET_SOURCE_URI, {"synthetic.glb", 13}, {}}
            : bl_AssetSource{BL_ASSET_SOURCE_BYTES, {}, {state.input.data(), state.input.size()}};
    return bl_loadGltf(engine, &source, out);
}

static void finish(State& state, bl_EngineContext engine)
{
    state.failAt = 0;
    require(bl_disposeRuntime(engine._runtime) == BL_OK, "CPU runtime teardown");
    bl_destroyAssetDecoder(state.decoder);
    require(!state.live && state.parses == state.releases && state.reads == state.readReleases,
            "tracked teardown0, source/parser/IO invocation-release balanced");
}

static void inspect(bl_EngineContext engine, bl_AssetContainer asset, const Json& expected)
{
    auto* owner = reinterpret_cast<L_Asset*>(l_peek(engine._runtime, asset._id));
    bl_AssetEntities entities = {};
    require(bl_getAssetContainerEntities(asset, &entities) == BL_OK && entities.count == 1 &&
                entities.data[0]._id == owner->root._id,
            "empty retains synthetic root identity");
    bl_String name = {};
    bl_Vec3 scale = {};
    bl_NodeChildren children = {};
    require(bl_getNodeName(entities.data[0], &name) == BL_OK && name.length == 8 &&
                !memcmp(name.data, "__root__", 8) &&
                bl_getNodeScaling(entities.data[0], &scale) == BL_OK && scale.x == -1 &&
                scale.y == 1 && scale.z == 1 &&
                bl_getNodeChildren(entities.data[0], &children) == BL_OK &&
                children.count == expected["rootChildren"].get<size_t>(),
            "source root reflection, selected scene only, no unused nodes imported");
    size_t meshes = 0;
    for (size_t i = 0; i < owner->nodeCount; ++i)
    {
        meshes += l_peek(engine._runtime, owner->nodes[i]._id)->kind == L_MESH;
    }
    require(meshes == expected["selectedPrimitives"].get<size_t>(),
            "actual selected primitives distinct from file-level unused meshes");
    if (expected["name"] == "camera-only-no-bin")
    {
        bl_Vec3 position = {};
        bl_Quat rotation = {};
        bl_NodeChildren descendants = {};
        require(children.count == 1 && bl_getNodeName(children.data[0], &name) == BL_OK &&
                    std::string(name.data, name.length) == "synthetic-camera-node" &&
                    bl_getNodePosition(children.data[0], &position) == BL_OK && position.x == 1 &&
                    position.y == 2 && position.z == 3 &&
                    bl_getNodeScaling(children.data[0], &scale) == BL_OK && scale.x == 2 &&
                    scale.y == 3 && scale.z == 4 &&
                    bl_getNodeRotationQuaternion(children.data[0], &rotation) == BL_OK &&
                    rotation.x == 0 && rotation.y == 0 && rotation.z == 0 && rotation.w == 1 &&
                    bl_getNodeChildren(children.data[0], &descendants) == BL_OK &&
                    descendants.count == 1,
                "camera-only node names, local TRS and children retained, no camera import");
        bl_Mat4 world = {};
        uint64_t version = 0;
        require(bl_getNodeWorldMatrix(descendants.data[0], &world, &version) == BL_OK &&
                    world.values[12] == -11 && world.values[13] == 20 && world.values[14] == 31,
                "camera node descendant inherits TRS and source root reflection");
    }
    bl_SceneContext scene = {};
    require(bl_createSceneContext(engine, &scene) == BL_OK &&
                bl_addAssetContainerToScene(scene, asset) == BL_OK &&
                bl_addAssetContainerToScene(scene, asset) == BL_OK,
            "ordinary duplicate container ownership semantics retained for empty");
    auto* native = reinterpret_cast<L_Scene*>(l_peek(engine._runtime, scene._id));
    require(native->memberCount == owner->nodeCount * 2 &&
                bl_disposeAssetContainer(asset) == BL_BUSY,
            "duplicates preserved, disposal remains busy with a scene owner");
    require(bl_removeAssetContainerFromScene(scene, asset) == BL_OK && native->memberCount == 0 &&
                bl_removeAssetContainerFromScene(scene, asset) == BL_OK &&
                bl_disposeScene(scene) == BL_OK,
            "symmetric idempotent empty removal");
    auto root = entities.data[0];
    require(bl_disposeAssetContainer(asset) == BL_OK && bl_disposeAssetContainer(asset) == BL_OK &&
                bl_getNodeName(root, &name) == BL_DISPOSED &&
                bl_getAssetContainerEntities(asset, &entities) == BL_DISPOSED,
            "idempotent disposal, stale asset/root never resurrect");
}

int main(int argc, char** argv)
{
    require(argc == 2, "usage EmptyTests fixture-directory");
    std::filesystem::path fixtures = argv[1];
    Json cases;
    std::ifstream(fixtures / "manifest.json") >> cases;
    size_t faults = 0;
    for (uint32_t version : {2u, 3u, 4u})
    {
        bl_ParsedGltf4 absent = {};
        auto* document = &absent.document3.document;
        document->structSize = version == 4   ? sizeof(absent)
                               : version == 3 ? sizeof(absent.document3)
                                              : sizeof(*document);
        document->version = version;
        document->gltfVersion = 2;
        require(l_validateAsset(document, nullptr, version >= 3 ? &absent.document3 : nullptr,
                                version == 4 ? &absent : nullptr) == BL_OK,
                "all absent raw descriptor arrays/counts may be NULL/zero");
        bl_AssetBufferView dangling = {0, 0, 4, {}};
        bl_AssetBufferViewInputs presence = {};
        document->bufferViews = &dangling;
        document->bufferViewCount = 1;
        absent.bufferViewInputs = &presence;
        require(l_validateAsset(document, nullptr, version >= 3 ? &absent.document3 : nullptr,
                                version == 4 ? &absent : nullptr) == BL_INVALID_ARGUMENT,
                "a view cannot reference an absent raw buffer even when no scene uses it");
        for (const auto& expected : cases)
        {
            State state;
            auto engine = setup(state, version);
            state.input = file(fixtures / (expected["name"].get<std::string>() + ".glb"));
            if (expected["selectedPrimitives"].get<size_t>())
            {
                bl_ParsedGltf4 full = {};
                auto* out = &full.document3.document;
                out->structSize = version == 4   ? sizeof(full)
                                  : version == 3 ? sizeof(full.document3)
                                                 : sizeof(*out);
                out->version = version;
                require(parse(&state, {state.input.data(), state.input.size()}, {}, out) == BL_OK,
                        "nonempty CPU path parses without attempting GPU creation");
                auto status =
                    l_validateAsset(out, nullptr, version >= 3 ? &full.document3 : nullptr,
                                    version == 4 ? &full : nullptr);
                auto wanted = expected["name"] == "implicit-zero-no-bin" && version != 4
                                  ? BL_UNSUPPORTED
                                  : BL_OK;
                require(status == wanted, "raw nonempty/implicit-zero accessor remains validated");
                if (status == BL_OK && expected["name"] == "implicit-zero-no-bin")
                {
                    L_AssetAccessorReader reader = {};
                    double values[3] = {1, 2, 3};
                    require(l_assetAccessorReader(out, 0, &full, &reader) == BL_OK &&
                                l_assetAccessorElement(&reader, 1, values) == BL_OK &&
                                values[0] == 0 && values[1] == 0 && values[2] == 0,
                            "legal no-buffer implicit-zero accessor resolves exact zeros");
                }
                releaseParsed(&state, out);
                finish(state, engine);
                continue;
            }
            for (bool uri : {false, true})
            {
                bl_AssetContainer asset = {engine._runtime, UINT64_MAX};
                auto status = load(state, engine, uri, &asset);
                auto wanted = static_cast<bl_Status>(expected["status"].get<unsigned>());
                if (expected["name"] == "implicit-zero-no-bin" && version != 4)
                {
                    wanted = BL_UNSUPPORTED;
                }
                if (status != wanted)
                {
                    fprintf(stderr, "Fixture %s v%u: status%d expected%d\n",
                            expected["name"].get<std::string>().c_str(), version, status, wanted);
                }
                require(status == wanted, "empty/no-BIN success or exact malformed status");
                if (status == BL_OK)
                {
                    inspect(engine, asset, expected);
                }
                else
                {
                    require(asset._id == UINT64_MAX, "failure publishes no partial identity");
                    if (expected["sourceErrorCode"] == 165)
                    {
                        bl_Error error = {};
                        require(bl_getLastError(engine._runtime, &error) == BL_OK &&
                                    error.sourceErrorCode == 165,
                                "missing required BIN error retained");
                    }
                }
            }
            require(state.parses == 2 && state.releases == 2 && state.reads == 1 &&
                        state.readReleases == 1,
                    "URI/BYTES exact-once calls");
            finish(state, engine);
        }
        for (size_t fault = 1; fault < 128; ++fault)
        {
            State state;
            auto engine = setup(state, version);
            state.input = file(fixtures / "camera-only-no-bin.glb");
            state.failAt = state.calls + fault;
            bl_AssetContainer asset = {engine._runtime, UINT64_MAX};
            auto status = load(state, engine, true, &asset);
            require(status == BL_OK || (status == BL_OUT_OF_MEMORY && asset._id == UINT64_MAX),
                    "no-buffer parser/Core allocation failures preserve output");
            finish(state, engine);
            if (status == BL_OK)
            {
                break;
            }
            ++faults;
        }
        for (auto partial : {BL_CANCELLED, BL_NOT_READY, BL_HOST_ERROR, BL_OUT_OF_MEMORY})
        {
            State state;
            auto engine = setup(state, version);
            state.input = file(fixtures / "camera-only-no-bin.glb");
            state.partialStatus = partial;
            bl_AssetContainer asset = {engine._runtime, UINT64_MAX};
            require(load(state, engine, false, &asset) == partial && asset._id == UINT64_MAX,
                    "partial parsed empty failure never publishes an asset");
            state.partialStatus = BL_OK;
            state.corrupt = true;
            require(load(state, engine, false, &asset) == BL_INVALID_ARGUMENT &&
                        asset._id == UINT64_MAX,
                    "malformed empty service prefix still rejected");
            finish(state, engine);
        }
    }
    require(faults > 10, "allocation fault sweeps executed, not vacuous");
    printf("Empty CPU: %zu assertions, services2/3/4, URI/BYTES, %zu OOM positions, teardown0\n",
           assertions, faults);
}
