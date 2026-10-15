#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include "AssetMotionInternal.h"
#include "PbrInternal.h"
#include <babylon_lite_asset_decoder.h>
#include <babylon_lite_shader_compiler.h>
#include "../LiteSandbox/AssetIO.h"
#include <renderdoc_app.h>
#include <json.hpp>
#include <filesystem>
#include <fstream>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

using Json = nlohmann::json;
static size_t assertions;
static size_t fences;

static void require(bool condition, const char* message)
{
    ++assertions;
    if (!condition)
    {
        fprintf(stderr, "Skin animation GPU: %s\n", message);
        exit(2);
    }
}

struct Context
{
    size_t live = 0;
    size_t calls = 0;
    size_t failAt = 0;
    bl_Runtime* runtime = nullptr;
    bl_EngineContext engine = {};
    bl_AssetDecoder* decoder = nullptr;
    bl_AssetService service = {};
    bl_ShaderCompilerService2 shader = bl_shaderCompilerService2();
    size_t compiles = 0;
    size_t compileReleases = 0;
    bl_Status shaderFailure = BL_OK;
    bool badBasisReflection = false;
};

static bl_Status compile(void* user, const bl_ShaderCompileRequest2* request,
                         bl_ShaderCompileResult2* result)
{
    auto* c = static_cast<Context*>(user);
    ++c->compiles;
    bl_Status status = c->shader.compile(c->shader.userData, request, result);
    std::string vertex(request->base.vertex.wgsl.data, request->base.vertex.wgsl.length);
    if (status == BL_OK && vertex.find("skinBitangents") != std::string::npos)
    {
        if (c->badBasisReflection)
        {
            auto* textures = const_cast<bl_ReflectedTexture2*>(result->textureInfo);
            bool found = false;
            for (size_t i = 0; i < result->textureInfoCount; ++i)
            {
                if (l_name(textures[i].binding.name, "skinBitangents"))
                {
                    textures[i].dimension = static_cast<bl_ShaderTextureDimension2>(UINT32_MAX);
                    found = true;
                }
            }
            require(found, "actual production basis reflection exists before corruption");
        }
        if (c->shaderFailure != BL_OK)
        {
            return c->shaderFailure;
        }
    }
    return status;
}

static void releaseCompiled(void* user, bl_ShaderCompileResult2* result)
{
    auto* c = static_cast<Context*>(user);
    ++c->compileReleases;
    c->shader.release(c->shader.userData, result);
}

static void* allocate(void* user, size_t bytes, size_t alignment)
{
    auto* c = static_cast<Context*>(user);
    if (++c->calls == c->failAt)
    {
        return nullptr;
    }
    void* result = _aligned_malloc(bytes, alignment);
    c->live += result != nullptr;
    return result;
}

static void deallocate(void* user, void* pointer, size_t, size_t)
{
    if (pointer)
    {
        --static_cast<Context*>(user)->live;
        _aligned_free(pointer);
    }
}

static std::vector<uint8_t> readTexture(bgfx::TextureHandle source, uint16_t width, uint16_t height,
                                        bgfx::TextureFormat::Enum format)
{
    auto target = bgfx::createTexture2D(width, height, false, 1, format,
                                        BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST);
    require(bgfx::isValid(target), "actual GPU readback storage");
    bgfx::TextureRegion from;
    bgfx::TextureRegion to;
    from.init(source);
    to.init(target);
    bgfx::blit(120, to, from);
    std::vector<uint8_t> result(size_t(width) * height *
                                (format == bgfx::TextureFormat::RGBA32F ? 16 : 4));
    uint32_t ready = bgfx::read(to, result.data());
    uint32_t current = bgfx::frame();
    while (static_cast<int32_t>(current - ready) < 0)
    {
        current = bgfx::frame();
    }
    bgfx::destroy(target);
    return result;
}

static bl_Status wait(void*)
{
    uint8_t marker[4] = {23, 47, 83, 255};
    auto source =
        bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0, bgfx::copy(marker, 4));
    auto read = readTexture(source, 1, 1, bgfx::TextureFormat::RGBA8);
    require(!memcmp(marker, read.data(), 4), "real submitted GPU completion marker");
    bgfx::destroy(source);
    ++fences;
    return BL_OK;
}

static bool initialized(void*)
{
    return true;
}

static std::vector<uint8_t> file(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}

static bl_Status load(Context& c, const std::filesystem::path& path, bool memory,
                      bl_AssetContainer* asset)
{
    std::string name = path.string();
    auto bytes = memory ? file(path) : std::vector<uint8_t>{};
    bl_AssetSource source = {memory ? BL_ASSET_SOURCE_BYTES : BL_ASSET_SOURCE_URI,
                             memory ? bl_String{} : bl_String{name.data(), name.size()},
                             {bytes.data(), bytes.size()}};
    return bl_loadGltf(c.engine, &source, asset);
}

static void numericNear(double value, double expected, const char* message)
{
    require(fabs(value - expected) <= 2e-5 + 2e-6 * fabs(expected), message);
}

static void setup(Context& c, bgfx::FrameBufferHandle target, uint16_t firstView = 0)
{
    bl_Allocator allocator = {&c, allocate, deallocate};
    require(bl_createAssetDecoder(&allocator, &c.decoder, &c.service) == BL_OK &&
                bl_assetDecoderService5(c.decoder, &c.service) == BL_OK,
            "environment provider5");
    bl_IOService io = SandboxAssetIO();
    bl_ShaderCompilerService2 shader = c.shader;
    shader.userData = &c;
    shader.compile = compile;
    shader.release = releaseCompiled;
    bl_RuntimeOptions2 options = {};
    options.structSize = sizeof(options);
    options.version = 2;
    options.base.allocator = allocator;
    options.base.io = &io;
    options.assets = &c.service;
    options.shaderCompiler2 = &shader;
    require(bl_createRuntime2(&options, &c.runtime) == BL_OK, "native runtime");
    require(bl_setGltfImportMode(c.runtime, BL_GLTF_IMPORT_NATIVE) == BL_OK,
            "direct cgltf construction without a raw6 service");
    bl_NativeEngineOptions native = {};
    native.ownership = BL_BGFX_BORROWED;
    native.backend = BL_RENDERER_D3D11;
    native.target = {BL_TARGET_BGFX_FRAMEBUFFER,
                     target.idx,
                     64,
                     64,
                     BL_COLOR_BGRA8,
                     BL_DEPTH_D24S8,
                     1,
                     firstView,
                     116};
    native.isExternalBgfxInitialized = initialized;
    native.waitForSubmittedWork = wait;
    require(bl_createEngine(c.runtime, &native, nullptr, &c.engine) == BL_OK, "native GPU engine");
}

static bl_SceneContext scene(Context& c, bl_AssetContainer asset,
                             const std::filesystem::path& lighting,
                             bl_Status registrationStatus = BL_OK)
{
    bl_SceneContext result = {};
    require(bl_createSceneContext(c.engine, &result) == BL_OK &&
                bl_addAssetContainerToScene(result, asset) == BL_OK,
            "asset scene ownership");
    std::string env = (lighting / "constant.env").string();
    std::string brdf = (lighting / "brdf-constant.png").string();
    bl_EnvironmentOptions options = {};
    options.skipGround = BL_BOOL_TRUE;
    options.skipSkybox = BL_BOOL_TRUE;
    options.brdfUrl = {brdf.data(), brdf.size()};
    bl_EnvironmentTextures environment = {};
    bl_Status environmentStatus =
        bl_loadEnvironment(result, {env.data(), env.size()}, &options, &environment);
    if (environmentStatus != BL_OK)
    {
        bl_Error error = {};
        bl_getLastError(c.runtime, &error);
        fprintf(stderr,
                "environment status%d shaderFailure%d badBasis%d failAt%zu calls%zu: %.*s\n",
                environmentStatus, c.shaderFailure, c.badBasisReflection, c.failAt, c.calls,
                int(error.message.length), error.message.data);
    }
    require(environmentStatus == BL_OK, "actual retained production lighting");
    bl_FreeCamera camera = {};
    require(bl_createFreeCamera(c.runtime, {0, 1, -6}, {0, 1, 0}, &camera) == BL_OK, "camera");
    bl_SceneProperties properties = {{.2, .3, .4, .25}, camera, 0};
    require(bl_setSceneProperties(result, &properties) == BL_OK, "scene state");
    bl_Status status = bl_registerScene(result);
    if (status != registrationStatus)
    {
        bl_Error error = {};
        bl_getLastError(c.runtime, &error);
        fprintf(stderr, "register%d: %.*s\n", status, int(error.message.length),
                error.message.data);
    }
    require(status == registrationStatus, "real production WGSL pipelines/status propagation");
    return result;
}

static bl_Status disposeScene(Context& c, bl_SceneContext handle)
{
    bl_SceneProperties properties = {};
    bl_Status status = bl_getSceneProperties(handle, &properties);
    std::vector<bl_EnvironmentTextures> environments;
    auto* native = reinterpret_cast<L_Scene*>(l_peek(c.runtime, handle._id));
    for (L_Environment* environment = native->environmentHistory; environment;
         environment = environment->next)
    {
        environments.push_back({c.runtime, environment->record.id});
    }
    if (status == BL_OK)
    {
        status = bl_disposeScene(handle);
    }
    if (status == BL_OK && properties.camera._id)
    {
        status = bl_disposeNode({c.runtime, properties.camera._id});
    }
    for (auto environment : environments)
    {
        if (status == BL_OK)
        {
            status = bl_disposeEnvironmentTextures(environment);
        }
    }
    return status;
}

static L_Mesh* skinned(Context& c, bl_AssetContainer asset)
{
    auto* owner = reinterpret_cast<L_Asset*>(l_peek(c.runtime, asset._id));
    L_Mesh* result = nullptr;
    for (size_t i = 0; i < owner->nodeCount; ++i)
    {
        auto* node = reinterpret_cast<L_Node*>(l_peek(c.runtime, owner->nodes[i]._id));
        if (node->record.kind == L_MESH)
        {
            auto* mesh = reinterpret_cast<L_Mesh*>(node);
            if (mesh->skin)
            {
                result = mesh;
            }
            else
            {
                require(bl_setNodeVisible(owner->nodes[i], false) == BL_OK,
                        "isolate actual skin draw, not replacement geometry");
            }
        }
    }
    require(result != nullptr, "genuine skin owner");
    return result;
}

static Json checkPose(Context& c, bl_AssetContainer asset, double seconds, bool mapped)
{
    L_Mesh* mesh = skinned(c, asset);
    auto& g = mesh->geometry;
    const double plain[9] = {0, 0, 0, 1, seconds, 0, 0, 1 + seconds / 2, 0};
    const double nonuniform[9] = {-1, 0, 0, 3, 1 + seconds, 0, 0, .5 + seconds / 2, 0};
    const double* expected = mapped ? nonuniform : plain;
    for (size_t i = 0; i < 9; ++i)
    {
        numericNear(g.streams[0][i], expected[i],
                    "independent joint/IBM/weights/source-mesh cancellation");
    }
    require(mesh->node.parent->record.id ==
                reinterpret_cast<L_Asset*>(l_peek(c.runtime, asset._id))->root._id,
            "drawable under import root once");
    double minimumX = expected[0];
    double maximumX = expected[0];
    double minimumY = expected[1];
    double maximumY = expected[1];
    for (size_t v = 1; v < 3; ++v)
    {
        minimumX = expected[v * 3] < minimumX ? expected[v * 3] : minimumX;
        maximumX = expected[v * 3] > maximumX ? expected[v * 3] : maximumX;
        minimumY = expected[v * 3 + 1] < minimumY ? expected[v * 3 + 1] : minimumY;
        maximumY = expected[v * 3 + 1] > maximumY ? expected[v * 3 + 1] : maximumY;
    }
    numericNear(g.minimum.x, minimumX, "deformed local bounds used by culling/framing");
    numericNear(g.maximum.x, maximumX, "deformed local maximum");
    numericNear(g.minimum.y, minimumY, "deformed local vertical bound");
    numericNear(g.maximum.y, maximumY, "moving deformation updates bounds beyond bind pose");
    bl_Vec3 worldMinimum = {};
    bl_Vec3 worldMaximum = {};
    bool hasBounds = false;
    require(l_meshWorldBounds(c.runtime, mesh, &worldMinimum, &worldMaximum, &hasBounds) == BL_OK &&
                hasBounds,
            "actual drawable world bounds");
    numericNear(worldMinimum.x, -maximumX, "world bounds apply root reflection only once");
    numericNear(worldMaximum.x, -minimumX, "world bounds ignore source mesh transform");
    numericNear(worldMaximum.y, maximumY, "world bounds follow animated vertices");
    Json record = {{"seconds", seconds},
                   {"mappedNonuniform", mapped},
                   {"stride", g.stride},
                   {"offsets", {g.offsets[0], g.offsets[1], g.offsets[4]}},
                   {"colorOffset", g.offsets[5]},
                   {"uvOffset", g.offsets[2]},
                   {"expectedPositions", std::vector<double>(expected, expected + 9)}};
    if (mapped)
    {
        const float tangents[9] = {2, 0, 0, 0, 1, 0, 1, .5f, 0};
        const float bitangents[12] = {0, 1, 0, 0, -2, 0, 0, 0, -1, .5f, 0, 0};
        for (size_t v = 0; v < 3; ++v)
        {
            numericNear(g.streams[1][v * 3 + 2], .5, "raw deformed normal magnitude retained");
            for (size_t k = 0; k < 3; ++k)
            {
                numericNear(g.streams[4][v * 4 + k], tangents[v * 3 + k],
                            "raw deformed tangent not renormalized");
            }
        }
        auto actual = readTexture(mesh->skin->bitangentTexture, mesh->skin->textureWidth,
                                  mesh->skin->textureHeight, bgfx::TextureFormat::RGBA32F);
        require(actual.size() == sizeof(bitangents), "actual private32F texture dimensions");
        for (size_t i = 0; i < 12; ++i)
        {
            float value;
            memcpy(&value, actual.data() + i * 4, 4);
            numericNear(value, bitangents[i], "actual GPU32F B is independent transformed basis");
        }
        record["expectedBitangents"] = std::vector<float>(bitangents, bitangents + 12);
    }
    return record;
}

static void sharedOwnership(Context& c, const std::filesystem::path& fixtures,
                            const std::filesystem::path& lighting)
{
    bl_AssetContainer asset = {};
    require(load(c, fixtures / "skin-four-clips.glb", true, &asset) == BL_OK, "shared skin load");
    auto* owner = reinterpret_cast<L_Asset*>(l_peek(c.runtime, asset._id));
    bl_AnimationGroups groups = {};
    bl_getAssetContainerAnimationGroups(asset, &groups);
    auto first = scene(c, asset, lighting);
    auto second = scene(c, asset, lighting);
    bl_SceneProperties properties = {};
    bl_getSceneProperties(first, &properties);
    properties.fixedDeltaMs = 10;
    bl_setSceneProperties(first, &properties);
    bl_getSceneProperties(second, &properties);
    properties.fixedDeltaMs = 20;
    bl_setSceneProperties(second, &properties);
    require(bl_renderFrame(c.engine, 500) == BL_OK, "two actual owning scene draws");
    bgfx::frame();
    bl_AnimationGroupInfo info = {};
    bl_getAnimationGroupInfo(groups.data[0], &info);
    numericNear(info.currentTime, .01, "earliest registered scene clock wins, no double tick");
    L_Node* child = owner->motion->nodes[2];
    bl_SceneNode parent = {};
    require(bl_getNodeParent({c.runtime, child->record.id}, &parent) == BL_OK &&
                parent._id == owner->motion->nodes[1]->record.id,
            "original joint parent");
    bl_Mat4 before = {};
    bl_Mat4 after = {};
    uint64_t version = 0;
    require(bl_getNodeWorldMatrix({c.runtime, child->record.id}, &before, &version) == BL_OK,
            "actual source-node world before owner removal");
    require(disposeScene(c, first) == BL_OK, "remove first actual scene owner");
    require(bl_getNodeParent({c.runtime, child->record.id}, &parent) == BL_OK &&
                parent._id == owner->motion->nodes[1]->record.id,
            "scene removal must not diverge protected hierarchy from skin palette");
    require(bl_getNodeWorldMatrix({c.runtime, child->record.id}, &after, &version) == BL_OK &&
                !memcmp(&before, &after, sizeof(before)),
            "shared source/attachment world retained");
    require(bl_renderFrame(c.engine, 500) == BL_OK, "remaining scene continues real deformation");
    bgfx::frame();
    bl_getAnimationGroupInfo(groups.data[0], &info);
    numericNear(info.currentTime, .03, "clock ownership transfers without wall-time catchup");
    require(bl_addAssetContainerToScene(second, asset) == BL_OK, "duplicate source ownership");
    require(bl_renderFrame(c.engine, 500) == BL_OK, "duplicate members real rendering");
    bgfx::frame();
    bl_getAnimationGroupInfo(groups.data[0], &info);
    numericNear(info.currentTime, .05, "duplicate add does not double clock");
    require(bl_removeAssetContainerFromScene(second, asset) == BL_OK,
            "remove duplicate memberships");
    auto* nativeScene = reinterpret_cast<L_Scene*>(l_peek(c.runtime, second._id));
    for (L_Callback* callback = nativeScene->before; callback; callback = callback->next)
    {
        require(callback->asset != owner, "no stale callback after all duplicate roots removed");
    }
    require(disposeScene(c, second) == BL_OK && bl_disposeAssetContainer(asset) == BL_OK,
            "last owner submits retirement and disposes motion");
}

static void resourceFailure(Context& c, const std::filesystem::path& fixtures,
                            const std::filesystem::path& lighting, bgfx::TextureHandle color)
{
    bl_AssetContainer asset = {};
    require(load(c, fixtures / "skin-mr-mapped-nonuniform-opaque.glb", true, &asset) == BL_OK,
            "resource invalidation fixture");
    L_Mesh* mesh = skinned(c, asset);
    auto sceneHandle = scene(c, asset, lighting);
    bl_AnimationGroups groups = {};
    bl_getAssetContainerAnimationGroups(asset, &groups);
    require(bl_goToFrame(groups.data[0], 30) == BL_OK && bl_renderFrame(c.engine, 0) == BL_OK,
            "resource fixture initialized at actual pose");
    bgfx::frame();
    wait(nullptr);
    bl_AnimationGroupInfo before = {};
    bl_getAnimationGroupInfo(groups.data[0], &before);
    std::vector<float> positions(mesh->geometry.streams[0], mesh->geometry.streams[0] + 9);
    auto buffer = mesh->geometry.vertexBuffer;
    mesh->geometry.vertexBuffer.idx = BL_INVALID_BGFX_HANDLE;
    require(bl_goToFrame(groups.data[0], 90) == BL_NOT_READY && mesh->skin->gpuDirty,
            "invalid dynamic resource marks model dirty before any state/upload commit");
    bl_AnimationGroupInfo after = {};
    bl_getAnimationGroupInfo(groups.data[0], &after);
    require(!memcmp(&before, &after, sizeof(before)) &&
                !memcmp(positions.data(), mesh->geometry.streams[0], 9 * sizeof(float)),
            "failed deformation keeps complete clock/pose state");
    mesh->geometry.vertexBuffer = buffer;
    require(bl_goToFrame(groups.data[0], 90) == BL_OK && !mesh->skin->gpuDirty &&
                bl_renderFrame(c.engine, 0) == BL_OK,
            "restored resource gets full dirty retry");
    bgfx::frame();
    wait(nullptr);
    checkPose(c, asset, 1.5, true);
    auto heldPixels = readTexture(color, 64, 64, bgfx::TextureFormat::BGRA8);
    std::vector<float> heldPositions(mesh->geometry.streams[0], mesh->geometry.streams[0] + 9);
    require(bl_stopAnimation(groups.data[0]) == BL_OK && bl_renderFrame(c.engine, 500) == BL_OK,
            "stopped source clock real frame");
    bgfx::frame();
    require(readTexture(color, 64, 64, bgfx::TextureFormat::BGRA8) == heldPixels &&
                !memcmp(heldPositions.data(), mesh->geometry.streams[0], 9 * sizeof(float)),
            "stop resets clock but holds actual deformed pose and GPU framebuffer");
    require(bl_playAnimation(groups.data[0]) == BL_OK && bl_renderFrame(c.engine, 0) == BL_OK,
            "play stopped clip evaluates first frame");
    bgfx::frame();
    checkPose(c, asset, 0, true);
    require(disposeScene(c, sceneHandle) == BL_OK && bl_disposeAssetContainer(asset) == BL_OK,
            "resource-retry submitted retirement");
}

static void compilerFailures(Context& c, const std::filesystem::path& fixtures,
                             const std::filesystem::path& lighting)
{
    for (unsigned fault = 0; fault < 3; ++fault)
    {
        printf("Compiler fault case%u\n", fault);
        fflush(stdout);
        bl_AssetContainer asset = {};
        require(load(c, fixtures / "skin-mr-mapped-nonuniform-opaque.glb", true, &asset) == BL_OK,
                "basis compiler failure fixture");
        auto handle = scene(c, asset, lighting);
        require(bl_unregisterScene(handle) == BL_OK, "isolate actual source-interface rebuild");
        auto* native = reinterpret_cast<L_Scene*>(l_peek(c.runtime, handle._id));
        l_cleanupPbrScene(c.runtime, native);
        c.shaderFailure = fault == 0 ? BL_CANCELLED : fault == 1 ? BL_NOT_READY : BL_OK;
        c.badBasisReflection = fault == 2;
        bl_Status expected = fault == 2 ? BL_SHADER_ERROR : c.shaderFailure;
        require(bl_registerScene(handle) == expected && !native->registered &&
                    c.compiles == c.compileReleases,
                "failed/partial actual compiler result released once, no scene publication");
        c.shaderFailure = BL_OK;
        c.badBasisReflection = false;
        require(bl_registerScene(handle) == BL_OK && bl_renderFrame(c.engine, 500) == BL_OK,
                "real source/reflection failure recovers without static-pose fallback");
        bgfx::frame();
        checkPose(c, asset, .5, true);
        require(disposeScene(c, handle) == BL_OK && bl_disposeAssetContainer(asset) == BL_OK,
                "compiler-failure/retry actual retirement");
    }
}

static std::vector<uint8_t> syntheticGlb(const Json& document, const std::vector<uint8_t>& binary)
{
    std::string text = document.dump();
    text.append((4 - text.size() % 4) % 4, ' ');
    bool hasBin = !binary.empty();
    uint32_t header[] = {0x46546c67, 2, uint32_t((hasBin ? 28 : 20) + text.size() + binary.size()),
                         uint32_t(text.size()), 0x4e4f534a};
    uint32_t bin[] = {uint32_t(binary.size()), 0x004e4942};
    std::vector<uint8_t> bytes(header[2]);
    memcpy(bytes.data(), header, sizeof(header));
    memcpy(bytes.data() + 20, text.data(), text.size());
    if (hasBin)
    {
        memcpy(bytes.data() + 20 + text.size(), bin, sizeof(bin));
        memcpy(bytes.data() + 28 + text.size(), binary.data(), binary.size());
    }
    return bytes;
}

static size_t integerAttribute(Json& document, std::vector<uint8_t>& binary, uint32_t component,
                               uint32_t value, bool normalized)
{
    size_t width = component == 5121 ? 1 : 2;
    while (binary.size() % 4)
    {
        binary.push_back(0);
    }
    size_t offset = binary.size();
    for (size_t v = 0; v < 3; ++v)
    {
        for (size_t lane = 0; lane < 4; ++lane)
        {
            uint16_t number = lane == 0 ? uint16_t(value) : 0;
            const auto* data = reinterpret_cast<const uint8_t*>(&number);
            binary.insert(binary.end(), data, data + width);
        }
    }
    size_t view = document["bufferViews"].size();
    document["bufferViews"].push_back(
        {{"buffer", 0}, {"byteOffset", offset}, {"byteLength", binary.size() - offset}});
    size_t accessor = document["accessors"].size();
    document["accessors"].push_back({{"bufferView", view},
                                     {"componentType", component},
                                     {"count", 3},
                                     {"type", "VEC4"},
                                     {"normalized", normalized}});
    document["buffers"][0]["byteLength"] = binary.size();
    return accessor;
}

static Json additionalInputs(Context& c, const std::filesystem::path& fixtures,
                             const std::filesystem::path& lighting,
                             const std::filesystem::path& output)
{
    auto base = file(fixtures / "skin-four-clips.glb");
    uint32_t jsonSize = 0;
    memcpy(&jsonSize, base.data() + 12, 4);
    Json original = Json::parse(base.begin() + 20, base.begin() + 20 + jsonSize);
    std::vector<uint8_t> payload(base.begin() + 28 + jsonSize, base.end());
    std::filesystem::create_directories(output / "independent-extra");
    Json receipts = Json::array();
    const char* names[] = {"singular-source-mesh-child",   "negative-source-mesh-child",
                           "camera-node-trs-child",        "invalid-skin-u32-max",
                           "invalid-joint-u32-max",        "invalid-skeleton-type",
                           "invalid-sampler-index-type",   "invalid-interpolation-type",
                           "unsupported-target-extension", "invalid-negative-skin",
                           "invalid-target-u32-max",       "mixed-integer-influences",
                           "invalid-mixed-integer-sum",    "invalid-mixed-duplicate-joint",
                           "no-bin-zero-duration-trs"};
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); ++index)
    {
        Json document = original;
        std::vector<uint8_t> binary = payload;
        bl_Status expected = index < 3 || index == 11 || index == 14 ? BL_OK
                             : index == 8                            ? BL_UNSUPPORTED
                                                                     : BL_INVALID_ARGUMENT;
        if (index < 2)
        {
            document["nodes"][3]["children"] = {6};
            document["nodes"].push_back({{"mesh", 0}, {"translation", {1, 2, 3}}});
            if (index == 1)
            {
                document["nodes"][3]["scale"] = {-1, 2, 3};
            }
        }
        else if (index == 2)
        {
            document.erase("skins");
            document["nodes"][3].erase("skin");
            document["nodes"][4]["children"] = {6};
            document["nodes"].push_back({{"mesh", 0}, {"translation", {0, 2, 0}}});
            document["animations"] = Json::array({document["animations"][0]});
            document["animations"][0]["channels"][0]["target"]["node"] = 4;
        }
        else if (index == 3)
        {
            document["nodes"][3]["skin"] = UINT32_MAX;
        }
        else if (index == 4)
        {
            document["skins"][0]["joints"] = {UINT32_MAX};
        }
        else if (index == 5)
        {
            document["skins"][0]["skeleton"] = "invalid";
        }
        else if (index == 6)
        {
            document["animations"][0]["samplers"][0]["input"] = true;
        }
        else if (index == 7)
        {
            document["animations"][0]["samplers"][0]["interpolation"] = true;
        }
        else if (index == 8)
        {
            document["animations"][0]["channels"][0]["target"] = {
                {"path", "translation"},
                {"extensions", {{"KHR_animation_pointer", {{"pointer", "/nodes/2/translation"}}}}}};
        }
        else if (index == 9)
        {
            document["nodes"][3]["skin"] = -1;
        }
        else if (index == 10)
        {
            document["animations"][0]["channels"][0]["target"]["node"] = UINT32_MAX;
        }
        else if (index < 14)
        {
            auto& attributes = document["meshes"][0]["primitives"][0]["attributes"];
            attributes["WEIGHTS_0"] = integerAttribute(document, binary, 5121, 128, true);
            attributes["JOINTS_1"] =
                integerAttribute(document, binary, 5121, index == 13 ? 0 : 1, false);
            attributes["WEIGHTS_1"] =
                integerAttribute(document, binary, 5123, index == 12 ? 32638 : 32639, true);
        }
        else
        {
            binary.clear();
            document = {
                {"asset", {{"version", "2.0"}}},
                {"nodes", {Json::object()}},
                {"scenes", {{{"nodes", {0}}}}},
                {"scene", 0},
                {"accessors",
                 {{{"componentType", 5126},
                   {"count", 1},
                   {"type", "SCALAR"},
                   {"min", {0}},
                   {"max", {0}}},
                  {{"componentType", 5126}, {"count", 1}, {"type", "VEC3"}}}},
                {"animations",
                 {{{"samplers", {{{"input", 0}, {"output", 1}}}},
                   {"channels",
                    {{{"sampler", 0}, {"target", {{"node", 0}, {"path", "translation"}}}}}}}}}};
        }
        document["asset"]["generator"] = "Independent native skin/TRS extra fixture; no user input";
        auto bytes = syntheticGlb(document, binary);
        auto path = output / "independent-extra" / (std::string(names[index]) + ".glb");
        std::ofstream(path, std::ios::binary)
            .write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        bl_AssetSource source = {BL_ASSET_SOURCE_BYTES, {}, {bytes.data(), bytes.size()}};
        bl_AssetContainer asset = {c.runtime, UINT64_MAX};
        bl_Status status = bl_loadGltf(c.engine, &source, &asset);
        if (status != expected)
        {
            fprintf(stderr, "%s got%d expected%d\n", names[index], status, expected);
        }
        require(status == expected, "independent typed/cancellation/mixed-format input");
        if (status == BL_OK)
        {
            auto handle = scene(c, asset, lighting);
            bl_AnimationGroups groups = {};
            bl_getAssetContainerAnimationGroups(asset, &groups);
            require(bl_goToFrame(groups.data[0], 60) == BL_OK &&
                        bl_renderFrame(c.engine, 0) == BL_OK,
                    "extra actual time-varying draw");
            bgfx::frame();
            auto* owner = reinterpret_cast<L_Asset*>(l_peek(c.runtime, asset._id));
            if (index < 3)
            {
                bl_Mat4 world = {};
                uint64_t version = 0;
                L_Node* child = owner->motion->nodes[6];
                require(bl_getNodeWorldMatrix({c.runtime, child->record.id}, &world, &version) ==
                            BL_OK,
                        "source child live-world query");
                numericNear(world.values[12],
                            index == 0   ? -9
                            : index == 1 ? -8
                                         : -1,
                            "mesh cancellation does not drop ordinary/camera source hierarchy");
                numericNear(world.values[13], index < 2 ? 11 : 5,
                            "source child authored transforms");
                numericNear(world.values[14], index < 2 ? 14 : 0, "source child authored depth");
                if (index < 2)
                {
                    checkPose(c, asset, 1, false);
                }
            }
            else if (index == 11)
            {
                L_Mesh* mesh = skinned(c, asset);
                numericNear(mesh->geometry.streams[0][1], 32639.0 / 65535,
                            "mixed sets normalized together, not independently");
                numericNear(mesh->geometry.streams[0][7], 1 + 32639.0 / 65535,
                            "all mixed-format influences actually deform vertices");
            }
            else
            {
                bl_AnimationGroupInfo info = {};
                require(groups.count == 1 &&
                            bl_setAnimationPlayback(groups.data[0], 1e308, true) == BL_OK &&
                            bl_playAnimation(groups.data[0]) == BL_OK &&
                            bl_renderFrame(c.engine, 1e308) == BL_OK &&
                            bl_getAnimationGroupInfo(groups.data[0], &info) == BL_OK &&
                            info.duration == 0 && info.currentTime == 0 && info.isPlaying,
                        "JSON-only empty scene retains zero-duration clip without overflow");
                bgfx::frame();
            }
            require(disposeScene(c, handle) == BL_OK && bl_disposeAssetContainer(asset) == BL_OK,
                    "extra fixture submitted retirement");
        }
        else
        {
            require(asset._id == UINT64_MAX, "extra failed-load output unchanged");
        }
        receipts.push_back({{"name", names[index]},
                            {"status", status},
                            {"provenance", "independent-synthetic-derived-from-independent-base"}});
    }
    return receipts;
}

static size_t callbackFailures(Context& c, const std::filesystem::path& fixtures)
{
    bl_AssetContainer baseline = {};
    require(load(c, fixtures / "skin-four-clips.glb", true, &baseline) == BL_OK,
            "callback allocation baseline");
    bl_SceneContext handle = {};
    require(bl_createSceneContext(c.engine, &handle) == BL_OK, "callback baseline scene");
    size_t before = c.calls;
    require(bl_addAssetContainerToScene(handle, baseline) == BL_OK, "callback baseline attachment");
    size_t count = c.calls - before;
    require(disposeScene(c, handle) == BL_OK && bl_disposeAssetContainer(baseline) == BL_OK,
            "callback baseline cleanup");
    for (size_t fault = 1; fault <= count; ++fault)
    {
        bl_AssetContainer asset = {};
        require(load(c, fixtures / "skin-four-clips.glb", true, &asset) == BL_OK &&
                    bl_createSceneContext(c.engine, &handle) == BL_OK,
                "callback failure fixture");
        c.failAt = c.calls + fault;
        bl_Status status = bl_addAssetContainerToScene(handle, asset);
        c.failAt = 0;
        require(status == BL_OUT_OF_MEMORY, "every new callback/owner/member allocation failure");
        auto* native = reinterpret_cast<L_Scene*>(l_peek(c.runtime, handle._id));
        for (L_Callback* callback = native->before; callback; callback = callback->next)
        {
            require(callback->asset == nullptr, "partial add never leaves a dangling motion hook");
        }
        require(disposeScene(c, handle) == BL_OK && bl_disposeAssetContainer(asset) == BL_OK,
                "source partial membership cleanup");
    }
    return count;
}

static Json churn(Context& c, const std::filesystem::path& fixtures,
                  const std::filesystem::path& lighting)
{
    size_t stable = 0;
    size_t maximum = 0;
    for (size_t iteration = 0; iteration < 250; ++iteration)
    {
        if (iteration % 25 == 0)
        {
            printf("Native churn%zu\n", iteration);
            fflush(stdout);
        }
        bl_AssetContainer asset = {};
        require(load(c, fixtures / "skin-four-clips.glb", true, &asset) == BL_OK,
                "native repeated load");
        auto handle = scene(c, asset, lighting);
        bl_AnimationGroups groups = {};
        bl_getAssetContainerAnimationGroups(asset, &groups);
        size_t selected = iteration % groups.count;
        require(bl_selectAssetContainerAnimation(asset, selected) == BL_OK &&
                    bl_goToFrame(groups.data[selected], 30) == BL_OK &&
                    bl_renderFrame(c.engine, 0) == BL_OK,
                "churn actual selected pose/draw");
        bgfx::frame();
        bl_AnimationGroup stale = groups.data[selected];
        require(disposeScene(c, handle) == BL_OK && bl_disposeAssetContainer(asset) == BL_OK &&
                    bl_playAnimation(stale) == BL_DISPOSED,
                "churn real retirement/stale generation");
        if (iteration == 50)
        {
            stable = c.live;
        }
        if (iteration > 50)
        {
            maximum = c.live > maximum ? c.live : maximum;
            require(c.live <= stable + 8, "motion/owner/record allocation plateau after warmup");
        }
    }
    return {{"iterations", 250}, {"liveAfterWarmup", stable}, {"maximumAfterWarmup", maximum}};
}

int main(int argc, char** argv)
{
    require(argc == 4, "synthetic directory, lighting directory, ignored output");
    std::filesystem::path fixtures(argv[1]);
    std::filesystem::path lighting(argv[2]);
    std::filesystem::path output = std::filesystem::absolute(argv[3]);
    std::filesystem::create_directories(output);
    HMODULE captureDll = LoadLibraryW(L"C:\\Program Files\\RenderDoc\\renderdoc.dll");
    require(captureDll != nullptr, "existing local RenderDoc tool");
    auto getApi =
        reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(captureDll, "RENDERDOC_GetAPI"));
    RENDERDOC_API_1_6_0* capture = nullptr;
    require(getApi && getApi(eRENDERDOC_API_Version_1_6_0, reinterpret_cast<void**>(&capture)) == 1,
            "local GPU capture API");
    require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "image COM");
    HWND window = CreateWindowExW(0, L"STATIC", L"Skin animation GPU", WS_POPUP, 0, 0, 64, 64,
                                  nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    bgfx::Init init;
    init.type = bgfx::RendererType::Direct3D11;
    init.swapChain.nwh = window;
    init.swapChain.width = 64;
    init.swapChain.height = 64;
    init.swapChain.formatColor = bgfx::TextureFormat::BGRA8;
    init.swapChain.formatDepthStencil = bgfx::TextureFormat::D24S8;
    // Serialize capture boundaries with actual D3D11 submission in this test host.
    bgfx::renderFrame();
    require(bgfx::init(init), "real production D3D11");
    bgfx::TextureHandle textures[2] = {
        bgfx::createTexture2D(64, 64, false, 1, bgfx::TextureFormat::BGRA8, BGFX_TEXTURE_RT),
        bgfx::createTexture2D(64, 64, false, 1, bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT)};
    auto framebuffer = bgfx::createFrameBuffer(2, textures, true);
    Context c;
    setup(c, framebuffer);
    sharedOwnership(c, fixtures, lighting);
    resourceFailure(c, fixtures, lighting, textures[0]);
    compilerFailures(c, fixtures, lighting);
    size_t callbackFailpoints = callbackFailures(c, fixtures);
    Json extraInputs = additionalInputs(c, fixtures, lighting, output);
    Json captures = Json::array();
    size_t rendered = 0;
    for (const char* name :
         {"skin-four-clips", "skin-mr-mapped-nonuniform-opaque", "skin-sg-mapped-nonuniform-blend"})
    {
        bl_AssetContainer asset = {};
        require(load(c, fixtures / (std::string(name) + ".glb"), true, &asset) == BL_OK,
                "actual runtime skinned source bytes");
        bl_AnimationGroups groups = {};
        require(bl_getAssetContainerAnimationGroups(asset, &groups) == BL_OK && groups.count == 4,
                "all four clips retained");
        bl_AnimationGroupInfo info = {};
        require(bl_getAnimationGroupInfo(groups.data[0], &info) == BL_OK && info.isPlaying &&
                    !info.isStopped && info.loopAnimation && info.currentTime == 0,
                "first autoplay default source state");
        require(bl_getAnimationGroupInfo(groups.data[1], &info) == BL_OK && !info.isPlaying &&
                    info.isStopped,
                "other clips stopped not discarded");
        L_Mesh* mesh = skinned(c, asset);
        require(bgfx::isValid(mesh->geometry.vertexBuffer), "actual dynamic vertex binding");
        auto sourceNode =
            reinterpret_cast<L_Asset*>(l_peek(c.runtime, asset._id))->motion->nodes[3];
        require(bl_setNodeParent({c.runtime, sourceNode->record.id}, {}) == BL_UNSUPPORTED,
                "protected source skin topology rejected before mutation");
        auto sceneHandle = scene(c, asset, lighting);
        require(bl_renderFrame(c.engine, 0) == BL_OK,
                "initialize actual production draw resources");
        bgfx::frame();
        wait(nullptr);
        require(bl_setAnimationPlayback(groups.data[0], 1, false) == BL_OK, "nonloop endpoint");
        for (double seconds : {0.0, .5, 1.5, 2.0})
        {
            std::string stem =
                (output / (std::string(name) + "-" + std::to_string(int(seconds * 10)))).string();
            capture->SetCaptureFilePathTemplate(stem.c_str());
            capture->StartFrameCapture(nullptr, nullptr);
            require(bl_goToFrame(groups.data[0], seconds * 60) == BL_OK,
                    "Core seek real time-varying deformation");
            require(bl_renderFrame(c.engine, 0) == BL_OK, "actual production rendered pose");
            bgfx::frame();
            wait(nullptr);
            require(capture->EndFrameCapture(nullptr, nullptr) == 1,
                    "actual draw capture completed");
            uint32_t length = 0;
            uint64_t timestamp = 0;
            uint32_t count = capture->GetNumCaptures();
            capture->GetCapture(count - 1, nullptr, &length, &timestamp);
            std::vector<char> path(length + 1);
            require(capture->GetCapture(count - 1, path.data(), &length, &timestamp) == 1,
                    "actual persisted capture");
            Json pose = checkPose(c, asset, seconds, std::string(name) != "skin-four-clips");
            pose["capture"] = path.data();
            auto pixels = readTexture(textures[0], 64, 64, bgfx::TextureFormat::BGRA8);
            std::ofstream(stem + ".bgra", std::ios::binary)
                .write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
            pose["pixels"] = stem + ".bgra";
            captures.push_back(pose);
            ++rendered;
        }
        if (std::string(name) != "skin-four-clips")
        {
            require(bl_selectAssetContainerAnimation(asset, 3) == BL_OK,
                    "select real rotation clip");
            for (double seconds : {0.0, 1.0, 0.0})
            {
                std::string stem =
                    (output / (std::string(name) + "-rotation-" + std::to_string(rendered)))
                        .string();
                capture->SetCaptureFilePathTemplate(stem.c_str());
                capture->StartFrameCapture(nullptr, nullptr);
                require(bl_goToFrame(groups.data[3], seconds * 60) == BL_OK &&
                            bl_renderFrame(c.engine, 0) == BL_OK,
                        "rotated and restored real GPU pose");
                bgfx::frame();
                wait(nullptr);
                require(capture->EndFrameCapture(nullptr, nullptr) == 1, "rotation capture");
                uint32_t length = 0;
                uint64_t timestamp = 0;
                uint32_t count = capture->GetNumCaptures();
                capture->GetCapture(count - 1, nullptr, &length, &timestamp);
                std::vector<char> path(length + 1);
                require(capture->GetCapture(count - 1, path.data(), &length, &timestamp) == 1,
                        "rotation capture persisted");
                double sine = seconds == 0 ? 0 : -1;
                double cosine = seconds == 0 ? 1 : 0;
                float expectedB[12] = {0,
                                       1,
                                       0,
                                       0,
                                       float(-2 * sine),
                                       float(cosine),
                                       0,
                                       0,
                                       float(-sine),
                                       float((1 + cosine) / 2),
                                       0,
                                       0};
                float expectedT[12] = {2,
                                       0,
                                       0,
                                       1,
                                       float(2 * cosine),
                                       float(sine),
                                       0,
                                       1,
                                       float(1 + cosine),
                                       float(sine / 2),
                                       0,
                                       1};
                auto actual = readTexture(mesh->skin->bitangentTexture, mesh->skin->textureWidth,
                                          mesh->skin->textureHeight, bgfx::TextureFormat::RGBA32F);
                for (size_t i = 0; i < 12; ++i)
                {
                    float value;
                    memcpy(&value, actual.data() + i * 4, 4);
                    numericNear(value, expectedB[i],
                                "mutable32F B changed and restored on actual GPU");
                }
                Json pose = {
                    {"seconds", seconds},
                    {"mappedNonuniform", true},
                    {"stride", mesh->geometry.stride},
                    {"offsets",
                     {mesh->geometry.offsets[0], mesh->geometry.offsets[1],
                      mesh->geometry.offsets[4]}},
                    {"colorOffset", mesh->geometry.offsets[5]},
                    {"uvOffset", mesh->geometry.offsets[2]},
                    {"expectedPositions",
                     {-1.0, 0.0, 0.0, 1 + 2 * sine, 1 - cosine, 0.0, -cosine, 1 - sine / 2, 0.0}},
                    {"expectedTangents", std::vector<float>(expectedT, expectedT + 12)},
                    {"capture", path.data()}};
                auto pixels = readTexture(textures[0], 64, 64, bgfx::TextureFormat::BGRA8);
                std::ofstream(stem + ".bgra", std::ios::binary)
                    .write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
                pose["pixels"] = stem + ".bgra";
                captures.push_back(pose);
                ++rendered;
            }
        }
        require(bl_selectAssetContainerAnimation(asset, 0) == BL_OK &&
                    bl_pauseAnimation(groups.data[0]) == BL_OK &&
                    bl_getAnimationGroupInfo(groups.data[0], &info) == BL_OK && !info.isStopped,
                "pause remains evaluator");
        require(bl_playAnimation(groups.data[1]) == BL_BUSY, "exclusive paused evaluator");
        for (size_t i = 0; i < groups.count; ++i)
        {
            require(bl_selectAssetContainerAnimation(asset, i) == BL_OK &&
                        bl_getAnimationGroupInfo(groups.data[i], &info) == BL_OK &&
                        info.isPlaying && !info.isStopped && info.currentTime == 0,
                    "every retained clip selectable and rest-reset");
        }
        require(bl_stopAnimation(groups.data[3]) == BL_OK &&
                    bl_getAnimationGroupInfo(groups.data[3], &info) == BL_OK && info.isStopped &&
                    info.currentTime == 0,
                "exact source stopped state");
        require(disposeScene(c, sceneHandle) == BL_OK, "scene release retires submitted skin");
        bl_AnimationGroup stale = groups.data[0];
        require(bl_disposeAssetContainer(asset) == BL_OK && bl_playAnimation(stale) == BL_DISPOSED,
                "borrowed clip identity invalidated");
    }
    size_t fixtureCases = 0;
    Json manifest = Json::parse(std::ifstream(fixtures / "fixtures.json"));
    for (const auto& fixture : manifest["fixtures"])
    {
        bl_AssetContainer asset = {c.runtime, UINT64_MAX};
        bl_Status status =
            load(c, fixtures / fixture["file"].get<std::string>(), fixtureCases % 2 != 0, &asset);
        std::string expected = fixture["expectedStatus"];
        bl_Status wanted = expected == "BL_OK"            ? BL_OK
                           : expected == "BL_UNSUPPORTED" ? BL_UNSUPPORTED
                                                          : BL_INVALID_ARGUMENT;
        if (status != wanted)
        {
            fprintf(stderr, "%s: got%d wanted%d\n", fixture["id"].get<std::string>().c_str(),
                    status, wanted);
        }
        require(status == wanted, "every labeled fixture actual URI/BYTES production loader");
        if (status == BL_OK)
        {
            auto handle = scene(c, asset, lighting);
            require(bl_renderFrame(c.engine, 0) == BL_OK, "real loaded fixture draw/empty clear");
            bgfx::frame();
            std::string fixtureName = fixture["id"];
            auto* owner = reinterpret_cast<L_Asset*>(l_peek(c.runtime, asset._id));
            if (fixtureName == "skin-two-skins-shared-source-mesh")
            {
                auto* first = owner->motion->deformations;
                auto* second = first->next;
                require(second && first->mesh != second->mesh &&
                            first->bind[0] != second->bind[0] &&
                            first->mesh->geometry.streams[0] != second->mesh->geometry.streams[0] &&
                            first->mesh->geometry.vertexBuffer.idx !=
                                second->mesh->geometry.vertexBuffer.idx,
                        "shared source mesh has independent mutable skin owners");
                auto* unchanged = first->skin == owner->motion->skins ? second : first;
                std::vector<float> original(unchanged->mesh->geometry.streams[0],
                                            unchanged->mesh->geometry.streams[0] + 9);
                bl_AnimationGroups groups = {};
                bl_getAssetContainerAnimationGroups(asset, &groups);
                require(bl_goToFrame(groups.data[0], 60) == BL_OK &&
                            bl_renderFrame(c.engine, 0) == BL_OK &&
                            !memcmp(original.data(), unchanged->mesh->geometry.streams[0],
                                    9 * sizeof(float)),
                        "animating one skin never overwrites another owner's deformation");
                bgfx::frame();
            }
            if (fixtureName.rfind("rotation-normalized-", 0) == 0)
            {
                bool negative = fixtureName.find("5120") != std::string::npos ||
                                fixtureName.find("5122") != std::string::npos;
                bl_AnimationGroups groups = {};
                bl_getAssetContainerAnimationGroups(asset, &groups);
                require(bl_goToFrame(groups.data[0], 60) == BL_OK &&
                            bl_renderFrame(c.engine, 0) == BL_OK,
                        "normalized integer rotation actually drives native playback");
                bgfx::frame();
                bl_Quat quaternion = {};
                require(bl_getNodeRotationQuaternion(
                            {c.runtime, owner->motion->nodes[2]->record.id}, &quaternion) == BL_OK,
                        "actual sampled normalized quaternion");
                numericNear(quaternion.z, (negative ? -1 : 1) * sqrt(.5),
                            "signed/unsigned normalized quaternion midpoint");
                auto* mesh = owner->motion->deformations->mesh;
                numericNear(mesh->geometry.streams[0][3], negative ? 0 : 2,
                            "normalized rotation actual deformed vertex");
                numericNear(mesh->geometry.streams[0][7], negative ? 1.5 : .5,
                            "normalized rotation blends all influences");
            }
            if (fixture["id"] == "cubic-zero-interpolant-runtime-error")
            {
                bl_AnimationGroups groups = {};
                bl_getAssetContainerAnimationGroups(asset, &groups);
                require(bl_goToFrame(groups.data[0], 60) == BL_INVALID_ARGUMENT,
                        "zero cubic interpolant rejected without hemisphere repair");
                bl_AnimationGroupInfo info = {};
                bl_getAnimationGroupInfo(groups.data[0], &info);
                require(info.currentTime == 0 && info.isPlaying, "failed seek atomic clock/state");
            }
            require(disposeScene(c, handle) == BL_OK && bl_disposeAssetContainer(asset) == BL_OK,
                    "loaded fixture real submitted cleanup");
        }
        else
        {
            require(asset._id == UINT64_MAX, "failed load preserves output canary");
        }
        ++fixtureCases;
    }
    for (bool memory : {false, true})
    {
        bl_AssetContainer reference = {};
        auto path = fixtures.parent_path() / "public-reference" / "RiggedSimple.glb";
        require(load(c, path, memory, &reference) == BL_OK,
                "original licensed public GLB URI/BYTES");
        auto handle = scene(c, reference, lighting);
        bl_AnimationGroups groups = {};
        require(bl_getAssetContainerAnimationGroups(reference, &groups) == BL_OK &&
                    groups.count == 1,
                "original public skin clip retained");
        require(bl_renderFrame(c.engine, 500) == BL_OK, "original public real animated draw");
        bgfx::frame();
        require(disposeScene(c, handle) == BL_OK && bl_disposeAssetContainer(reference) == BL_OK,
                "public original teardown");
    }
    size_t loadAllocations = 0;
    Json allocationSweeps = Json::array();
    for (const char* faultFixture : {"skin-four-clips.glb", "skin-mr-mapped-nonuniform-opaque.glb"})
    {
        Context allocationBaseline;
        setup(allocationBaseline, framebuffer, 128);
        size_t before = allocationBaseline.calls;
        bl_AssetContainer baselineAsset = {};
        require(load(allocationBaseline, fixtures / faultFixture, true, &baselineAsset) == BL_OK,
                "Core allocation baseline");
        size_t allocations = allocationBaseline.calls - before;
        loadAllocations += allocations;
        require(bl_disposeAssetContainer(baselineAsset) == BL_OK &&
                    bl_disposeRuntime(allocationBaseline.runtime) == BL_OK,
                "allocation baseline teardown");
        bl_destroyAssetDecoder(allocationBaseline.decoder);
        require(allocationBaseline.live == 0, "allocation baseline0");
        for (size_t fault = 1; fault <= allocations; ++fault)
        {
            Context trial;
            setup(trial, framebuffer, 128);
            trial.failAt = trial.calls + fault;
            bl_AssetContainer unchanged = {trial.runtime, UINT64_MAX};
            bl_Status status = load(trial, fixtures / faultFixture, true, &unchanged);
            if (status != BL_OUT_OF_MEMORY)
            {
                fprintf(stderr, "%s allocator fault%zu got%d\n", faultFixture, fault, status);
            }
            require(status == BL_OUT_OF_MEMORY && unchanged._id == UINT64_MAX,
                    "every Core/parser/motion/basis load allocation fails atomically");
            trial.failAt = 0;
            require(bl_disposeRuntime(trial.runtime) == BL_OK, "partial native load retirement");
            bl_destroyAssetDecoder(trial.decoder);
            require(trial.live == 0, "all fault-trial allocations0");
        }
        allocationSweeps.push_back({{"fixture", faultFixture}, {"failpoints", allocations}});
    }
    Json churnResult = churn(c, fixtures, lighting);
    std::ofstream(output / "captures.json") << captures.dump(2);
    require(bl_disposeRuntime(c.runtime) == BL_OK, "runtime actual retirement");
    bl_destroyAssetDecoder(c.decoder);
    require(c.live == 0 && c.compiles == c.compileReleases,
            "all Core/provider allocations0 and actual compiler results released once");
    Json result = {{"state", "GPU_RENDER_AND_TEXTURE_READBACK_PASSED_VERTEX_CAPTURE_PENDING"},
                   {"captureHostSingleThreaded", true},
                   {"renderedPoses", rendered},
                   {"assertions", assertions},
                   {"fixtureCases", fixtureCases},
                   {"loadAllocatorFailpoints", loadAllocations},
                   {"allocationSweeps", allocationSweeps},
                   {"compiles", c.compiles},
                   {"compileReleases", c.compileReleases},
                   {"extraIndependentInputs", extraInputs},
                   {"callbackAllocatorFailpoints", callbackFailpoints},
                   {"churn", churnResult},
                   {"actualFences", fences},
                   {"liveAllocations", c.live}};
    std::ofstream(output / "result.json") << result.dump(2);
    bgfx::destroy(framebuffer);
    wait(nullptr);
    bgfx::shutdown();
    DestroyWindow(window);
    CoUninitialize();
    printf("Skin GPU: %zu production poses, %zu assertions, %zu actual fences\n", rendered,
           assertions, fences);
}
