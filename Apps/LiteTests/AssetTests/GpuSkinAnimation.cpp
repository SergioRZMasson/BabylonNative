#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include "AssetMotionInternal.h"
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
};

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

static std::vector<uint8_t> readTexture(bgfx::TextureHandle source, uint16_t width,
                                       uint16_t height, bgfx::TextureFormat::Enum format)
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
    auto source = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8,
                                        0, bgfx::copy(marker, 4));
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

static void setup(Context& c, bgfx::FrameBufferHandle target)
{
    bl_Allocator allocator = {&c, allocate, deallocate};
    require(bl_createAssetDecoder(&allocator, &c.decoder, &c.service) == BL_OK &&
                bl_assetDecoderService6(c.decoder, &c.service) == BL_OK, "real raw6 decoder");
    bl_IOService io = SandboxAssetIO();
    bl_ShaderCompilerService2 shader = bl_shaderCompilerService2();
    bl_RuntimeOptions2 options = {};
    options.structSize = sizeof(options);
    options.version = 2;
    options.base.allocator = allocator;
    options.base.io = &io;
    options.assets = &c.service;
    options.shaderCompiler2 = &shader;
    require(bl_createRuntime2(&options, &c.runtime) == BL_OK, "native runtime");
    bl_NativeEngineOptions native = {};
    native.ownership = BL_BGFX_BORROWED;
    native.backend = BL_RENDERER_D3D11;
    native.target = {BL_TARGET_BGFX_FRAMEBUFFER, target.idx, 64, 64, BL_COLOR_BGRA8,
                     BL_DEPTH_D24S8, 1, 0, 116};
    native.isExternalBgfxInitialized = initialized;
    native.waitForSubmittedWork = wait;
    require(bl_createEngine(c.runtime, &native, nullptr, &c.engine) == BL_OK, "native GPU engine");
}

static bl_SceneContext scene(Context& c, bl_AssetContainer asset,
                             const std::filesystem::path& lighting)
{
    bl_SceneContext result = {};
    require(bl_createSceneContext(c.engine, &result) == BL_OK &&
                bl_addAssetContainerToScene(result, asset) == BL_OK, "asset scene ownership");
    std::string env = (lighting / "constant.env").string();
    std::string brdf = (lighting / "brdf-constant.png").string();
    bl_EnvironmentOptions options = {};
    options.skipGround = BL_BOOL_TRUE;
    options.skipSkybox = BL_BOOL_TRUE;
    options.brdfUrl = {brdf.data(), brdf.size()};
    bl_EnvironmentTextures environment = {};
    require(bl_loadEnvironment(result, {env.data(), env.size()}, &options, &environment) == BL_OK,
            "actual retained production lighting");
    bl_FreeCamera camera = {};
    require(bl_createFreeCamera(c.runtime, {0, 1, -6}, {0, 1, 0}, &camera) == BL_OK, "camera");
    bl_SceneProperties properties = {{.2, .3, .4, .25}, camera, 0};
    require(bl_setSceneProperties(result, &properties) == BL_OK, "scene state");
    bl_Status status = bl_registerScene(result);
    if (status != BL_OK)
    {
        bl_Error error = {};
        bl_getLastError(c.runtime, &error);
        fprintf(stderr, "register%d: %.*s\n", status, int(error.message.length), error.message.data);
    }
    require(status == BL_OK, "real production WGSL pipelines");
    return result;
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
    require(mesh->node.parent->record.id == reinterpret_cast<L_Asset*>(
                l_peek(c.runtime, asset._id))->root._id, "drawable under import root once");
    Json record = {{"seconds", seconds}, {"mappedNonuniform", mapped},
                   {"stride", g.stride}, {"offsets", {g.offsets[0], g.offsets[1], g.offsets[4]}},
                   {"colorOffset", g.offsets[5]}, {"uvOffset", g.offsets[2]},
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

int main(int argc, char** argv)
{
    require(argc == 4, "synthetic directory, lighting directory, ignored output");
    std::filesystem::path fixtures(argv[1]);
    std::filesystem::path lighting(argv[2]);
    std::filesystem::path output = std::filesystem::absolute(argv[3]);
    std::filesystem::create_directories(output);
    HMODULE captureDll = LoadLibraryW(L"C:\\Program Files\\RenderDoc\\renderdoc.dll");
    require(captureDll != nullptr, "existing local RenderDoc tool");
    auto getApi = reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(captureDll, "RENDERDOC_GetAPI"));
    RENDERDOC_API_1_6_0* capture = nullptr;
    require(getApi && getApi(eRENDERDOC_API_Version_1_6_0,
                            reinterpret_cast<void**>(&capture)) == 1, "local GPU capture API");
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
    require(bgfx::init(init), "real production D3D11");
    bgfx::TextureHandle textures[2] = {
        bgfx::createTexture2D(64, 64, false, 1, bgfx::TextureFormat::BGRA8, BGFX_TEXTURE_RT),
        bgfx::createTexture2D(64, 64, false, 1, bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT)};
    auto framebuffer = bgfx::createFrameBuffer(2, textures, true);
    Context c;
    setup(c, framebuffer);
    Json captures = Json::array();
    size_t rendered = 0;
    for (const char* name : {"skin-four-clips", "skin-mr-mapped-nonuniform-opaque",
                             "skin-sg-mapped-nonuniform-blend"})
    {
        bl_AssetContainer asset = {};
        require(load(c, fixtures / (std::string(name) + ".glb"), true, &asset) == BL_OK,
                "actual runtime skinned source bytes");
        bl_AnimationGroups groups = {};
        require(bl_getAssetContainerAnimationGroups(asset, &groups) == BL_OK && groups.count == 4,
                "all four clips retained");
        bl_AnimationGroupInfo info = {};
        require(bl_getAnimationGroupInfo(groups.data[0], &info) == BL_OK &&
                    info.isPlaying && !info.isStopped && info.loopAnimation &&
                    info.currentTime == 0, "first autoplay default source state");
        require(bl_getAnimationGroupInfo(groups.data[1], &info) == BL_OK &&
                    !info.isPlaying && info.isStopped, "other clips stopped not discarded");
        L_Mesh* mesh = skinned(c, asset);
        require(bgfx::isValid(mesh->geometry.vertexBuffer), "actual dynamic vertex binding");
        auto sourceNode = reinterpret_cast<L_Asset*>(l_peek(c.runtime, asset._id))->motion->nodes[3];
        require(bl_setNodeParent({c.runtime, sourceNode->record.id}, {}) == BL_UNSUPPORTED,
                "protected source skin topology rejected before mutation");
        auto sceneHandle = scene(c, asset, lighting);
        require(bl_setAnimationPlayback(groups.data[0], 1, false) == BL_OK, "nonloop endpoint");
        for (double seconds : {0.0, .5, 1.5, 2.0})
        {
            std::string stem = (output / (std::string(name) + "-" +
                                          std::to_string(int(seconds * 10)))).string();
            capture->SetCaptureFilePathTemplate(stem.c_str());
            capture->StartFrameCapture(nullptr, nullptr);
            require(bl_goToFrame(groups.data[0], seconds * 60) == BL_OK,
                    "Core seek real time-varying deformation");
            require(bl_renderFrame(c.engine, 0) == BL_OK, "actual production rendered pose");
            bgfx::frame();
            wait(nullptr);
            require(capture->EndFrameCapture(nullptr, nullptr) == 1, "actual draw capture completed");
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
            std::ofstream(stem + ".bgra", std::ios::binary).write(
                reinterpret_cast<const char*>(pixels.data()), pixels.size());
            pose["pixels"] = stem + ".bgra";
            captures.push_back(pose);
            ++rendered;
        }
        if (std::string(name) != "skin-four-clips")
        {
            require(bl_selectAssetContainerAnimation(asset, 3) == BL_OK, "select real rotation clip");
            for (double seconds : {0.0, 1.0, 0.0})
            {
                std::string stem = (output / (std::string(name) + "-rotation-" +
                    std::to_string(rendered))).string();
                capture->SetCaptureFilePathTemplate(stem.c_str());
                capture->StartFrameCapture(nullptr, nullptr);
                require(bl_goToFrame(groups.data[3], seconds * 60) == BL_OK &&
                            bl_renderFrame(c.engine, 0) == BL_OK, "rotated and restored real GPU pose");
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
                float expectedB[12] = {0, 1, 0, 0, float(-2*sine), float(cosine), 0, 0,
                                       float(-sine), float((1+cosine)/2), 0, 0};
                float expectedT[12] = {2, 0, 0, 1, float(2*cosine), float(sine), 0, 1,
                                       float(1+cosine), float(sine/2), 0, 1};
                auto actual = readTexture(mesh->skin->bitangentTexture, mesh->skin->textureWidth,
                                           mesh->skin->textureHeight, bgfx::TextureFormat::RGBA32F);
                for (size_t i = 0; i < 12; ++i)
                {
                    float value;
                    memcpy(&value, actual.data() + i*4, 4);
                    numericNear(value, expectedB[i], "mutable32F B changed and restored on actual GPU");
                }
                Json pose = {{"seconds", seconds}, {"mappedNonuniform", true},
                    {"stride", mesh->geometry.stride},
                    {"offsets", {mesh->geometry.offsets[0], mesh->geometry.offsets[1],
                                  mesh->geometry.offsets[4]}},
                    {"colorOffset", mesh->geometry.offsets[5]}, {"uvOffset", mesh->geometry.offsets[2]},
                    {"expectedPositions", {-1.0, 0.0, 0.0, 1+2*sine, 1-cosine, 0.0,
                                           -cosine, 1-sine/2, 0.0}},
                    {"expectedTangents", std::vector<float>(expectedT, expectedT+12)},
                    {"capture", path.data()}};
                auto pixels = readTexture(textures[0], 64, 64, bgfx::TextureFormat::BGRA8);
                std::ofstream(stem + ".bgra", std::ios::binary).write(
                    reinterpret_cast<const char*>(pixels.data()), pixels.size());
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
        size_t fixtureCases = 0;
        Json manifest = Json::parse(std::ifstream(fixtures / "fixtures.json"));
        for (const auto& fixture : manifest["fixtures"])
        {
            bl_AssetContainer asset = {c.runtime, UINT64_MAX};
            bl_Status status = load(c, fixtures / fixture["file"].get<std::string>(),
                                     fixtureCases % 2 != 0, &asset);
            std::string expected = fixture["expectedStatus"];
            bl_Status wanted = expected == "BL_OK" ? BL_OK
                               : expected == "BL_UNSUPPORTED" ? BL_UNSUPPORTED : BL_INVALID_ARGUMENT;
            if (status != wanted)
            {
                fprintf(stderr, "%s: got%d wanted%d\n",
                        fixture["id"].get<std::string>().c_str(), status, wanted);
            }
            require(status == wanted, "every labeled fixture actual URI/BYTES production loader");
            if (status == BL_OK)
            {
                auto handle = scene(c, asset, lighting);
                require(bl_renderFrame(c.engine, 0) == BL_OK, "real loaded fixture draw/empty clear");
                bgfx::frame();
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
                require(bl_disposeScene(handle) == BL_OK && bl_disposeAssetContainer(asset) == BL_OK,
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
            require(load(c, path, memory, &reference) == BL_OK, "original licensed public GLB URI/BYTES");
            auto handle = scene(c, reference, lighting);
            bl_AnimationGroups groups = {};
            require(bl_getAssetContainerAnimationGroups(reference, &groups) == BL_OK && groups.count == 1,
                    "original public skin clip retained");
            require(bl_renderFrame(c.engine, 500) == BL_OK, "original public real animated draw");
            bgfx::frame();
            require(bl_disposeScene(handle) == BL_OK && bl_disposeAssetContainer(reference) == BL_OK,
                    "public original teardown");
        }
        Context allocationBaseline;
        setup(allocationBaseline, framebuffer);
        size_t before = allocationBaseline.calls;
        bl_AssetContainer baselineAsset = {};
        require(load(allocationBaseline, fixtures / "skin-four-clips.glb", true, &baselineAsset) == BL_OK,
                "Core allocation baseline");
        size_t loadAllocations = allocationBaseline.calls - before;
        require(bl_disposeAssetContainer(baselineAsset) == BL_OK &&
                    bl_disposeRuntime(allocationBaseline.runtime) == BL_OK, "allocation baseline teardown");
        bl_destroyAssetDecoder(allocationBaseline.decoder);
        require(allocationBaseline.live == 0, "allocation baseline0");
        for (size_t fault = 1; fault <= loadAllocations; ++fault)
        {
            Context trial;
            setup(trial, framebuffer);
            trial.failAt = trial.calls + fault;
            bl_AssetContainer unchanged = {trial.runtime, UINT64_MAX};
            bl_Status status = load(trial, fixtures / "skin-four-clips.glb", true, &unchanged);
            if (status != BL_OUT_OF_MEMORY)
            {
                fprintf(stderr, "load allocator fault%zu got%d\n", fault, status);
            }
            require(status == BL_OUT_OF_MEMORY && unchanged._id == UINT64_MAX,
                    "every Core/provider load allocation fails atomically");
            trial.failAt = 0;
            require(bl_disposeRuntime(trial.runtime) == BL_OK, "partial native load retirement");
            bl_destroyAssetDecoder(trial.decoder);
            require(trial.live == 0, "all fault-trial allocations0");
        }
        require(bl_stopAnimation(groups.data[3]) == BL_OK &&
                    bl_getAnimationGroupInfo(groups.data[3], &info) == BL_OK &&
                    info.isStopped && info.currentTime == 0, "exact source stopped state");
        require(bl_disposeScene(sceneHandle) == BL_OK, "scene release retires submitted skin");
        bl_AnimationGroup stale = groups.data[0];
        require(bl_disposeAssetContainer(asset) == BL_OK &&
                    bl_playAnimation(stale) == BL_DISPOSED, "borrowed clip identity invalidated");
    }
    std::ofstream(output / "captures.json") << captures.dump(2);
    require(bl_disposeRuntime(c.runtime) == BL_OK, "runtime actual retirement");
    bl_destroyAssetDecoder(c.decoder);
    require(c.live == 0, "all Core/provider allocations0");
    Json result = {{"state", "GPU_RENDER_AND_TEXTURE_READBACK_PASSED_VERTEX_CAPTURE_PENDING"},
                   {"renderedPoses", rendered}, {"assertions", assertions},
                   {"fixtureCases", fixtureCases},
                   {"loadAllocatorFailpoints", loadAllocations},
                   {"actualFences", fences}, {"liveAllocations", c.live}};
    std::ofstream(output / "result.json") << result.dump(2);
    bgfx::destroy(framebuffer);
    wait(nullptr);
    bgfx::shutdown();
    DestroyWindow(window);
    CoUninitialize();
    printf("Skin GPU: %zu production poses, %zu assertions, %zu actual fences\n",
           rendered, assertions, fences);
}
