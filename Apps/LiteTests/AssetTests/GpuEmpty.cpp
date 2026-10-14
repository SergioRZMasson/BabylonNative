#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include "AssetInternal.h"
#include <babylon_lite_asset_decoder.h>
#include <babylon_lite_shader_compiler.h>
#include "../LiteSandbox/AssetIO.h"
#include "../LiteSandbox/SceneCamera.h"
#include <json.hpp>
#include <filesystem>
#include <fstream>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

using Json = nlohmann::json;
static size_t assertions = 0;
static size_t retirements = 0;

static void require(bool condition, const char* message)
{
    ++assertions;
    if (!condition)
    {
        fprintf(stderr, "Empty scene GPU: %s\n", message);
        exit(2);
    }
}

struct Context
{
    size_t live = 0;
    size_t bytes = 0;
    size_t calls = 0;
    size_t failAt = 0;
    size_t parses = 0;
    size_t releases = 0;
    size_t reads = 0;
    size_t readReleases = 0;
    size_t environments = 0;
    size_t environmentReleases = 0;
    bl_AssetDecoder* decoder = nullptr;
    bl_AssetService raw = {};
    bl_IOService io = SandboxAssetIO();
    bl_Runtime* runtime = nullptr;
    bl_EngineContext engine = {};
    const bl_ParsedGltf* prefix = nullptr;
};

static void* allocate(void* user, size_t bytes, size_t alignment)
{
    auto* context = static_cast<Context*>(user);
    if (++context->calls == context->failAt)
    {
        return nullptr;
    }
    void* result = _aligned_malloc(bytes, alignment);
    if (result)
    {
        ++context->live;
        context->bytes += bytes;
    }
    return result;
}

static void release(void* user, void* pointer, size_t bytes, size_t)
{
    if (pointer)
    {
        auto* context = static_cast<Context*>(user);
        --context->live;
        context->bytes -= bytes;
        _aligned_free(pointer);
    }
}

static std::vector<uint8_t> capture(bgfx::TextureHandle color)
{
    auto target = bgfx::createTexture2D(64, 64, false, 1, bgfx::TextureFormat::BGRA8,
                                        BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST);
    require(bgfx::isValid(target), "actual staging resource");
    bgfx::TextureRegion from;
    bgfx::TextureRegion to;
    from.init(color);
    to.init(target);
    bgfx::blit(120, to, from);
    std::vector<uint8_t> pixels(64 * 64 * 4);
    uint32_t ready = bgfx::read(to, pixels.data());
    uint32_t frame = bgfx::frame();
    while (static_cast<int32_t>(frame - ready) < 0)
    {
        frame = bgfx::frame();
    }
    bgfx::destroy(target);
    return pixels;
}

static bl_Status wait(void*)
{
    const uint8_t marker[4] = {17, 43, 73, 255};
    auto source =
        bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0, bgfx::copy(marker, 4));
    auto target = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8,
                                        BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST);
    require(bgfx::isValid(source) && bgfx::isValid(target), "real retirement fence resources");
    bgfx::TextureRegion from;
    bgfx::TextureRegion to;
    from.init(source);
    to.init(target);
    bgfx::blit(126, to, from);
    uint8_t result[4] = {};
    uint32_t ready = bgfx::read(to, result);
    uint32_t frame = bgfx::frame();
    while (static_cast<int32_t>(frame - ready) < 0)
    {
        frame = bgfx::frame();
    }
    require(!memcmp(result, marker, 4), "submitted GPU work completes before resource retirement");
    ++retirements;
    bgfx::destroy(source);
    bgfx::destroy(target);
    return BL_OK;
}

static bool initialized(void*)
{
    return true;
}

static bl_Status parse(void* user, bl_Bytes bytes, bl_String base, bl_ParsedGltf* out)
{
    auto* context = static_cast<Context*>(user);
    ++context->parses;
    context->prefix = out;
    return context->raw.parseGltf(context->raw.userData, bytes, base, out);
}

static void releaseParsed(void* user, bl_ParsedGltf* out)
{
    auto* context = static_cast<Context*>(user);
    require(context->prefix == out, "original raw prefix released exactly once");
    ++context->releases;
    context->raw.releaseGltf(context->raw.userData, out);
}

static bl_Status parseEnvironment(void* user, bl_Bytes bytes, bl_ParsedEnvironment* out)
{
    auto* context = static_cast<Context*>(user);
    ++context->environments;
    return context->raw.parseEnvironment(context->raw.userData, bytes, out);
}

static void releaseEnvironment(void* user, bl_ParsedEnvironment* out)
{
    auto* context = static_cast<Context*>(user);
    ++context->environmentReleases;
    context->raw.releaseEnvironment(context->raw.userData, out);
}

static bl_Status read(void* user, bl_String path, bl_HostBuffer* out)
{
    auto* context = static_cast<Context*>(user);
    ++context->reads;
    return context->io.readFile(context->io.userData, path, out);
}

static void releaseRead(void* user, bl_HostBuffer* out)
{
    auto* context = static_cast<Context*>(user);
    ++context->readReleases;
    context->io.releaseBuffer(context->io.userData, out);
}

static void setup(Context& context, bgfx::FrameBufferHandle framebuffer)
{
    bl_Allocator allocator = {&context, allocate, release};
    require(bl_createAssetDecoder(&allocator, &context.decoder, &context.raw) == BL_OK &&
                bl_assetDecoderService4(context.decoder, &context.raw) == BL_OK,
            "raw service4");
    auto assets = context.raw;
    assets.userData = &context;
    assets.parseGltf = parse;
    assets.releaseGltf = releaseParsed;
    assets.parseEnvironment = parseEnvironment;
    assets.releaseEnvironment = releaseEnvironment;
    auto io = context.io;
    io.userData = &context;
    io.readFile = read;
    io.releaseBuffer = releaseRead;
    auto compiler = bl_shaderCompilerService2();
    bl_RuntimeOptions2 options = {};
    options.structSize = sizeof(options);
    options.version = 2;
    options.base.allocator = allocator;
    options.base.io = &io;
    options.assets = &assets;
    options.shaderCompiler2 = &compiler;
    require(bl_createRuntime2(&options, &context.runtime) == BL_OK, "tracked runtime");
    bl_NativeEngineOptions native = {};
    native.ownership = BL_BGFX_BORROWED;
    native.backend = BL_RENDERER_D3D11;
    native.target = {BL_TARGET_BGFX_FRAMEBUFFER,
                     framebuffer.idx,
                     64,
                     64,
                     BL_COLOR_BGRA8,
                     BL_DEPTH_D24S8,
                     1,
                     0,
                     116};
    native.isExternalBgfxInitialized = initialized;
    native.waitForSubmittedWork = wait;
    require(bl_createEngine(context.runtime, &native, nullptr, &context.engine) == BL_OK &&
                bl_startEngine(context.engine, nullptr, nullptr) == BL_OK,
            "actual D3D11 engine");
}

static void finish(Context& context)
{
    context.failAt = 0;
    require(bl_disposeRuntime(context.runtime) == BL_OK, "runtime real submitted-work teardown");
    bl_destroyAssetDecoder(context.decoder);
    require(!context.live && !context.bytes && context.parses == context.releases &&
                context.reads == context.readReleases &&
                context.environments == context.environmentReleases,
            "tracked teardown0 and exact-once IO/parser/environment");
}

static std::vector<uint8_t> file(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "fixture input exists");
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(input), {});
}

static bl_Status load(Context& context, const std::filesystem::path& path, bool uri,
                      bl_AssetContainer* out)
{
    auto bytes = file(path);
    std::string name = path.string();
    bl_AssetSource source =
        uri ? bl_AssetSource{BL_ASSET_SOURCE_URI, {name.data(), name.size()}, {}}
            : bl_AssetSource{BL_ASSET_SOURCE_BYTES, {}, {bytes.data(), bytes.size()}};
    return bl_loadGltf(context.engine, &source, out);
}

struct Model
{
    bl_AssetContainer asset = {};
    bl_SceneContext scene = {};
    bl_ArcRotateCamera camera = {};
    bl_ArcRotateControl control = {};
    bl_EnvironmentTextures environment = {};
    bl_SceneNode root = {};
};

static void dispose(Model& model)
{
    require(bl_detachControl(model.control) == BL_OK && bl_disposeScene(model.scene) == BL_OK &&
                bl_disposeNode({model.camera._runtime, model.camera._id}) == BL_OK &&
                bl_disposeAssetContainer(model.asset) == BL_OK,
            "replace all prior lifetimes");
    if (model.environment._id)
    {
        require(bl_disposeEnvironmentTextures(model.environment) == BL_OK, "environment teardown");
    }
    model = {};
}

static Model model(Context& context, bl_AssetContainer asset, bool empty,
                   const std::filesystem::path& lighting)
{
    Model result;
    result.asset = asset;
    bl_AssetEntities entities = {};
    require(bl_getAssetContainerEntities(asset, &entities) == BL_OK && entities.count == 1,
            "selected scene retains real root");
    result.root = entities.data[0];
    require(bl_createSceneContext(context.engine, &result.scene) == BL_OK &&
                bl_addAssetContainerToScene(result.scene, asset) == BL_OK,
            "candidate ownership");
    if (!empty)
    {
        std::string env = (lighting / "environmentSpecular.env").string();
        std::string brdf = (lighting / "brdf-lut.png").string();
        bl_EnvironmentOptions options = {};
        options.brdfUrl = {brdf.data(), brdf.size()};
        options.skipGround = BL_BOOL_TRUE;
        options.skipSkybox = BL_BOOL_TRUE;
        require(bl_loadEnvironment(result.scene, {env.data(), env.size()}, &options,
                                   &result.environment) == BL_OK,
                "unchanged PBR lighting service");
    }
    require(sandboxSceneCamera(result.scene, empty, &result.camera) == BL_OK,
            "explicit host empty policy or unchanged source default camera");
    bl_ArcRotateCameraProperties camera = {};
    require(bl_getArcRotateCameraProperties(result.camera, &camera) == BL_OK &&
                isfinite(camera.radius) && camera.radius > 0 && camera.nearPlane > 0 &&
                camera.farPlane > camera.nearPlane,
            "safe finite actual camera");
    if (empty)
    {
        require(camera.radius == 1 && camera.target.x == 0 && camera.target.y == 0 &&
                    camera.target.z == 0 && camera.nearPlane == .01 && camera.farPlane == 1000,
                "empty camera explicit documented host policy");
    }
    bl_SceneProperties2 properties = {};
    require(bl_getSceneProperties2(result.scene, &properties) == BL_OK,
            "candidate camera assigned");
    properties.clearColor = {.2, .4, .6, 1};
    require(bl_setSceneProperties2(result.scene, &properties) == BL_OK &&
                bl_attachControl(result.camera, result.scene, nullptr, &result.control) == BL_OK &&
                bl_registerScene(result.scene) == BL_OK,
            "candidate camera/control preparation");
    return result;
}

static size_t changedPixels(const std::vector<uint8_t>& pixels)
{
    const uint8_t clear[4] = {153, 102, 51, 255};
    size_t changed = 0;
    for (size_t i = 0; i < pixels.size(); i += 4)
    {
        changed += memcmp(pixels.data() + i, clear, 4) != 0;
    }
    return changed;
}

static std::vector<uint8_t> frame(Context& context, bgfx::TextureHandle color, size_t draws)
{
    require(bl_frame(context.engine, 16.6666666667) == BL_OK, "real render/clear frame");
    bl_EngineStats stats = {};
    require(bl_getEngineStats(context.engine, &stats) == BL_OK && stats.drawCallCount == draws,
            "actual draws, never process-only or old-geometry success");
    auto pixels = capture(color);
    require(draws ? changedPixels(pixels) > 50 : changedPixels(pixels) == 0,
            "nonempty really rendered; empty exact full-frame BGRA clear without old geometry");
    return pixels;
}

int main(int argc, char** argv)
{
    require(argc == 4, "usage GPUEmpty fixture-directory lighting-directory output-directory");
    std::filesystem::path fixtures = argv[1];
    std::filesystem::path lighting = argv[2];
    std::filesystem::path output = argv[3];
    std::filesystem::create_directories(output);
    require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "COM for existing image IO");
    HWND window =
        CreateWindowExW(0, L"STATIC", L"Lite empty-scene GPU correctness", WS_OVERLAPPEDWINDOW, 0,
                        0, 80, 80, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    require(window != nullptr, "real host window");
    bgfx::Init init;
    init.type = bgfx::RendererType::Direct3D11;
    init.swapChain.nwh = window;
    init.swapChain.width = 64;
    init.swapChain.height = 64;
    init.swapChain.formatColor = bgfx::TextureFormat::BGRA8;
    init.swapChain.formatDepthStencil = bgfx::TextureFormat::D24S8;
    require(bgfx::init(init), "actual D3D11 bgfx");
    bgfx::TextureHandle textures[2] = {
        bgfx::createTexture2D(64, 64, false, 1, bgfx::TextureFormat::BGRA8, BGFX_TEXTURE_RT),
        bgfx::createTexture2D(64, 64, false, 1, bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT)};
    auto framebuffer = bgfx::createFrameBuffer(2, textures, true);
    require(bgfx::isValid(framebuffer), "real color/depth framebuffer");
    Context context;
    setup(context, framebuffer);
    Json cases;
    std::ifstream(fixtures / "manifest.json") >> cases;
    Model active;
    std::vector<uint8_t> reference;
    size_t successful = 0;
    size_t failures = 0;
    size_t churn = 0;
    Json evidence = Json::array();
    auto replace = [&](const std::string& name, bool empty, bool uri) {
        bl_AssetContainer asset = {};
        require(load(context, fixtures / (name + ".glb"), uri, &asset) == BL_OK,
                "actual raw URI/BYTES load");
        auto candidate = model(context, asset, empty, lighting);
        if (active.scene._id)
        {
            require(bl_unregisterScene(active.scene) == BL_OK,
                    "old scene stopped before candidate frame");
        }
        auto pixels = frame(context, textures[0], empty ? 0 : 1);
        if (name == "accepted-triangle")
        {
            if (reference.empty())
            {
                reference = pixels;
            }
            require(pixels == reference, "nonempty recovery produces identical actual GPU frame");
        }
        if (active.scene._id)
        {
            auto stale = active.root;
            dispose(active);
            bl_String unused = {};
            require(bl_getNodeName(stale, &unused) == BL_DISPOSED,
                    "replaced root identity is stale");
        }
        active = candidate;
        ++successful;
        evidence.push_back({{"fixture", name},
                            {"empty", empty},
                            {"draws", empty ? 0 : 1},
                            {"changedPixels", changedPixels(pixels)},
                            {"asset", asset._id}});
        if (successful <= 4)
        {
            std::ofstream image(output / (name + ".bgra"), std::ios::binary);
            image.write(reinterpret_cast<const char*>(pixels.data()),
                        static_cast<std::streamsize>(pixels.size()));
        }
    };
    replace("accepted-triangle", false, false);
    for (const auto& expected : cases)
    {
        const std::string name = expected["name"];
        if (expected["status"] == 0 && expected["selectedPrimitives"] == 0)
        {
            replace(name, true, true);
            replace("accepted-triangle", false, false);
        }
        else if (expected["status"] != 0)
        {
            bl_AssetContainer sentinel = {context.runtime, UINT64_MAX};
            require(load(context, fixtures / (name + ".glb"), true, &sentinel) ==
                            static_cast<bl_Status>(expected["status"].get<unsigned>()) &&
                        sentinel._id == UINT64_MAX,
                    "malformed input preserves active model/output");
            require(frame(context, textures[0], 1) == reference,
                    "failure preserves actual old geometry");
            ++failures;
        }
    }
    {
        bl_AssetContainer zero = {};
        require(load(context, fixtures / "implicit-zero-no-bin.glb", false, &zero) == BL_OK,
                "legal zero-base POSITION without any buffer loads on the actual GPU path");
        auto candidate = model(context, zero, false, lighting);
        require(bl_unregisterScene(active.scene) == BL_OK &&
                    bl_frame(context.engine, 16.6666666667) == BL_OK,
                "degenerate zero-position primitive is not mistaken for a missing resource");
        bl_EngineStats stats = {};
        require(bl_getEngineStats(context.engine, &stats) == BL_OK && stats.drawCallCount == 1 &&
                    changedPixels(capture(textures[0])) == 0,
                "zero-base degenerate primitive submits its real draw, without a proxy triangle");
        dispose(active);
        active = candidate;
        replace("accepted-triangle", false, false);
    }
    for (bool empty : {false, true})
    {
        bl_ArcRotateCamera untouched = {context.runtime, UINT64_MAX};
        require(sandboxSceneCamera({context.runtime, UINT64_MAX}, empty, &untouched) ==
                        BL_INVALID_HANDLE &&
                    untouched._id == UINT64_MAX,
                "real invalid scene/camera errors propagate, never empty-scene fallback success");
    }
    size_t cameraFaults = 0;
    for (size_t fault = 1; fault < 32; ++fault)
    {
        bl_SceneContext scene = {};
        require(bl_createSceneContext(context.engine, &scene) == BL_OK, "camera fault scene");
        bl_ArcRotateCamera camera = {context.runtime, UINT64_MAX};
        context.failAt = context.calls + fault;
        auto status = sandboxSceneCamera(scene, true, &camera);
        context.failAt = 0;
        require(status == BL_OK || (status == BL_OUT_OF_MEMORY && camera._id == UINT64_MAX),
                "empty camera OOM propagates unchanged output");
        require(bl_disposeScene(scene) == BL_OK, "camera failure scene cleanup");
        if (status == BL_OK)
        {
            require(bl_disposeNode({context.runtime, camera._id}) == BL_OK,
                    "camera endpoint disposal");
            break;
        }
        ++cameraFaults;
    }
    size_t oom = 0;
    for (size_t fault = 1; fault < 128; ++fault)
    {
        context.failAt = context.calls + fault;
        bl_AssetContainer sentinel = {context.runtime, UINT64_MAX};
        auto status = load(context, fixtures / "camera-only-no-bin.glb", false, &sentinel);
        context.failAt = 0;
        require(status == BL_OK || (status == BL_OUT_OF_MEMORY && sentinel._id == UINT64_MAX),
                "every empty parser/Core OOM leaves no partial output");
        require(frame(context, textures[0], 1) == reference,
                "empty OOM never damages active GPU geometry");
        if (status == BL_OK)
        {
            require(bl_disposeAssetContainer(sentinel) == BL_OK,
                    "successful fault endpoint disposal");
            break;
        }
        ++oom;
    }
    size_t warmBytes = 0;
    size_t maximumBytes = 0;
    for (unsigned cycle = 0; cycle < 20; ++cycle)
    {
        replace("camera-only-no-bin", true, cycle % 2 == 0);
        replace("accepted-triangle", false, cycle % 2 != 0);
        ++churn;
        if (cycle == 4)
        {
            warmBytes = context.bytes;
        }
        if (cycle >= 4 && context.bytes > maximumBytes)
        {
            maximumBytes = context.bytes;
        }
    }
    require(maximumBytes == warmBytes, "repeated empty/geometry replacement plateaus after warmup");
    bl_ArcRotateInput wheel = {};
    wheel.kind = BL_ARC_WHEEL;
    wheel.deltaY = -120;
    bl_ArcRotateInputEffects effects = {};
    require(bl_processArcRotateInput(active.control, &wheel, &effects) == BL_OK,
            "recovery retains functioning native camera controls");
    dispose(active);
    finish(context);
    Json report = {{"assertions", assertions},
                   {"successfulLoads", successful},
                   {"malformedOrUnsupported", failures},
                   {"churnCycles", churn},
                   {"oomPositions", oom},
                   {"actualRetirementFences", retirements},
                   {"cameraOomPositions", cameraFaults},
                   {"implicitZeroNoBufferGpuLoad", true},
                   {"warmTrackedBytes", warmBytes},
                   {"maximumTrackedBytes", maximumBytes},
                   {"trackedTeardownBytes", 0},
                   {"parses", context.parses},
                   {"parserReleases", context.releases},
                   {"reads", context.reads},
                   {"readReleases", context.readReleases},
                   {"cases", evidence},
                   {"productionShaderModified", false}};
    std::ofstream(output / "result.json") << report.dump(2) << '\n';
    bgfx::destroy(framebuffer);
    wait(nullptr);
    bgfx::shutdown();
    DestroyWindow(window);
    CoUninitialize();
    printf(
        "Empty GPU: %zu loads, %zu rejects, %zu OOM positions, %zu churn, %zu fences, teardown0\n",
        successful, failures, oom, churn, retirements);
}
