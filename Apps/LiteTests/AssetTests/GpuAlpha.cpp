#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include "AssetInternal.h"
#include "PbrInternal.h"
#include <babylon_lite_asset_decoder.h>
#include <babylon_lite_shader_compiler.h>
#include "../LiteSandbox/AssetIO.h"
#include <filesystem>
#include <fstream>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

static void require(bool value, const char* message)
{
    if (!value)
    {
        fprintf(stderr, "GPU alpha: %s\n", message);
        exit(2);
    }
}

struct Memory
{
    size_t live = 0;
    size_t bytes = 0;
};

static void* allocate(void* user, size_t size, size_t alignment)
{
    void* result = _aligned_malloc(size, alignment);
    if (result)
    {
        auto* memory = static_cast<Memory*>(user);
        ++memory->live;
        memory->bytes += size;
    }
    return result;
}

static void release(void* user, void* pointer, size_t bytes, size_t)
{
    if (pointer)
    {
        auto* memory = static_cast<Memory*>(user);
        --memory->live;
        memory->bytes -= bytes;
        _aligned_free(pointer);
    }
}

static bool initialized(void*)
{
    return true;
}

static bl_Status wait(void*)
{
    const uint8_t marker[4] = {13, 31, 61, 255};
    uint8_t pixels[4] = {};
    auto source =
        bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0, bgfx::copy(marker, 4));
    auto target = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8,
                                        BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST);
    require(bgfx::isValid(source) && bgfx::isValid(target), "retirement fence");
    bgfx::TextureRegion from;
    from.init(source);
    bgfx::TextureRegion to;
    to.init(target);
    bgfx::blit(126, to, from);
    uint32_t ready = bgfx::read(to, pixels);
    uint32_t frame = bgfx::frame();
    while (static_cast<int32_t>(frame - ready) < 0)
    {
        frame = bgfx::frame();
    }
    require(!memcmp(pixels, marker, 4), "actual submitted retirement completion");
    bgfx::destroy(source);
    bgfx::destroy(target);
    return BL_OK;
}

struct Compiler
{
    bl_ShaderCompilerService2 service = bl_shaderCompilerService2();
    size_t calls = 0;
    size_t releases = 0;
    size_t maskCotangent = 0;
    std::filesystem::path output;
};

static bl_Status compile(void* user, const bl_ShaderCompileRequest2* request,
                         bl_ShaderCompileResult2* result)
{
    auto* compiler = static_cast<Compiler*>(user);
    ++compiler->calls;
    std::string fragment(request->base.fragment.wgsl.data, request->base.fragment.wgsl.length);
    if (fragment.find("discard;") != std::string::npos &&
        fragment.find("cotangentFrame") != std::string::npos)
    {
        ++compiler->maskCotangent;
    }
    std::ofstream source(compiler->output /
                         ("production-" + std::to_string(compiler->calls) + ".wgsl"));
    source << fragment;
    // Observe only: the production compiler receives byte-identical WGSL, including final alpha.
    return compiler->service.compile(compiler->service.userData, request, result);
}

static void releaseCompiled(void* user, bl_ShaderCompileResult2* result)
{
    auto* compiler = static_cast<Compiler*>(user);
    ++compiler->releases;
    compiler->service.release(compiler->service.userData, result);
}

static bl_String text(const std::string& value)
{
    return {value.data(), value.size()};
}

static std::vector<uint8_t> capture(bgfx::TextureHandle source)
{
    auto staging = bgfx::createTexture2D(64, 64, false, 1, bgfx::TextureFormat::BGRA8,
                                         BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST);
    require(bgfx::isValid(staging), "actual RGBA staging target");
    bgfx::TextureRegion from;
    from.init(source);
    bgfx::TextureRegion to;
    to.init(staging);
    bgfx::blit(120, to, from);
    std::vector<uint8_t> bytes(64 * 64 * 4);
    uint32_t ready = bgfx::read(to, bytes.data());
    uint32_t frame = bgfx::frame();
    while (static_cast<int32_t>(frame - ready) < 0)
    {
        frame = bgfx::frame();
    }
    bgfx::destroy(staging);
    return bytes;
}

static void render(const std::filesystem::path& model, const std::filesystem::path& lighting,
                   const std::filesystem::path& output, bgfx::FrameBufferHandle framebuffer,
                   bgfx::TextureHandle color, Compiler& compiler)
{
    Memory memory;
    bl_Allocator allocator = {&memory, allocate, release};
    bl_AssetDecoder* decoder = nullptr;
    bl_AssetService assets = {};
    require(bl_createAssetDecoder(&allocator, &decoder, &assets) == BL_OK &&
                bl_assetDecoderService3(decoder, &assets) == BL_OK,
            "raw v3 decoder");
    auto io = SandboxAssetIO();
    auto shader = compiler.service;
    shader.userData = &compiler;
    shader.compile = compile;
    shader.release = releaseCompiled;
    bl_RuntimeOptions2 options = {};
    options.structSize = sizeof(options);
    options.version = BL_RUNTIME_OPTIONS_VERSION_2;
    options.base.allocator = allocator;
    options.base.io = &io;
    options.assets = &assets;
    options.shaderCompiler2 = &shader;
    bl_Runtime* runtime = nullptr;
    require(bl_createRuntime2(&options, &runtime) == BL_OK, "tracked runtime");
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
    bl_EngineContext engine = {};
    require(bl_createEngine(runtime, &native, nullptr, &engine) == BL_OK, "framebuffer engine");
    auto path = model.string();
    bl_AssetSource source = {BL_ASSET_SOURCE_URI, text(path), {}};
    bl_AssetContainer asset = {};
    require(bl_loadGltf(engine, &source, &asset) == BL_OK, "actual imported alpha material");
    bl_SceneContext scene = {};
    require(bl_createSceneContext(engine, &scene) == BL_OK &&
                bl_addAssetContainerToScene(scene, asset) == BL_OK,
            "scene ownership");
    auto environmentPath = (lighting / "environmentSpecular.env").string();
    auto brdf = (lighting / "brdf-lut.png").string();
    bl_EnvironmentOptions environmentOptions = {};
    environmentOptions.brdfUrl = text(brdf);
    environmentOptions.skipGround = BL_BOOL_TRUE;
    environmentOptions.skipSkybox = BL_BOOL_TRUE;
    bl_EnvironmentTextures environment = {};
    require(bl_loadEnvironment(scene, text(environmentPath), &environmentOptions, &environment) ==
                BL_OK,
            "existing actual IBL lighting");
    bl_FreeCamera camera = {};
    require(bl_createFreeCamera(runtime, {0, 0, -3}, {0, 0, 0}, &camera) == BL_OK, "fixed camera");
    bl_SceneProperties properties = {{.2, .4, .6, .25}, camera, 0};
    require(bl_setSceneProperties(scene, &properties) == BL_OK && bl_registerScene(scene) == BL_OK,
            "production shader preparation");
    auto* owner = reinterpret_cast<L_Asset*>(l_peek(runtime, asset._id));
    auto* nativeScene = reinterpret_cast<L_Scene*>(l_peek(runtime, scene._id));
    std::ofstream state(output / (model.stem().string() + ".json"));
    state << "{\"materials\":[";
    size_t materialCount = 0;
    for (size_t i = 1; i < owner->materialCount; ++i)
    {
        auto* material = reinterpret_cast<L_Material*>(l_peek(runtime, owner->materials[i]._id));
        if (!material)
        {
            continue;
        }
        bl_PbrMaterialInfo info = {};
        require(bl_getPbrMaterialInfo({runtime, material->record.id}, &info) == BL_OK &&
                    info.alphaBlend == material->blending,
                "public effective alpha blend state");
        uint64_t bits = l_pbrDrawState(material, -1, true);
        require(bool(bits & BGFX_STATE_WRITE_Z) == !material->blending &&
                    (bits & BGFX_STATE_DEPTH_TEST_MASK) == BGFX_STATE_DEPTH_TEST_GEQUAL,
                "draw state uses depth test, opaque/mask writes and no blend writes");
        require((bits & BGFX_STATE_BLEND_MASK) ==
                    (material->blending
                         ? BGFX_STATE_BLEND_FUNC_SEPARATE(
                               BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA,
                               BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA)
                         : 0),
                "straight color and source-over alpha draw factors");
        require((bits & BGFX_STATE_CULL_MASK) == (material->culling ? BGFX_STATE_CULL_CW : 0) &&
                    !(l_pbrDrawState(material, 1, false) &
                      (BGFX_STATE_DEPTH_TEST_MASK | BGFX_STATE_WRITE_Z)),
                "existing mirror culling and no-depth target state preserved");
        if (materialCount++)
        {
            state << ',';
        }
        state << "{\"blend\":" << material->blending << ",\"mask\":" << material->testing
              << ",\"cutoff\":" << material->pbrAlphaCutoff << ",\"alpha\":" << info.alpha
              << ",\"baseImage\":" << material->pbrBaseImage << ",\"state\":" << bits << "}";
    }
    bl_Mat4 view;
    l_identity(&view);
    size_t count = 0;
    // Production collection and stable depth-before-order sort, with contradictory explicit orders.
    for (size_t i = 0; i < nativeScene->memberCount; ++i)
    {
        auto* node = nativeScene->members[i];
        if (node->record.kind == L_MESH)
        {
            auto* mesh = reinterpret_cast<L_Mesh*>(node);
            mesh->properties.renderOrder = {true, node->parent->position.z > 0 ? 999.0 : -999.0};
            if (model.stem() == "equal-depth-orders")
            {
                auto identity = l_meshMaterial(mesh);
                auto* material = reinterpret_cast<L_Material*>(l_peek(runtime, identity._id));
                mesh->properties.renderOrder.value =
                    material->pbrEmissiveFactor.x > material->pbrEmissiveFactor.z ? 999 : -999;
            }
        }
    }
    require(l_collectDraws(runtime, nativeScene, &view, &count) == BL_OK, "actual draw collection");
    l_sortDraws(nativeScene, count);
    auto* draws = static_cast<L_Draw*>(nativeScene->drawScratch);
    state << "],\"orderedDraws\":[";
    bool transparent = false;
    double depth = INFINITY;
    for (size_t i = 0; i < count; ++i)
    {
        require(!transparent || draws[i].transparent, "opaque/mask precede transparent draws");
        if (draws[i].transparent)
        {
            require(draws[i].depth <= depth, "transparent depth precedes contradictory order");
            if (i && draws[i - 1].transparent && draws[i].depth == draws[i - 1].depth)
            {
                require(draws[i].order >= draws[i - 1].order &&
                            (draws[i].order != draws[i - 1].order ||
                             draws[i].sequence > draws[i - 1].sequence),
                        "equal-depth transparent order then stable insertion ties");
            }
            depth = draws[i].depth;
            transparent = true;
        }
        if (i)
        {
            state << ',';
        }
        state << "{\"transparent\":" << draws[i].transparent << ",\"depth\":" << draws[i].depth
              << ",\"order\":" << draws[i].order << ",\"sequence\":" << draws[i].sequence << "}";
    }
    require(bl_startEngine(engine, nullptr, nullptr) == BL_OK, "render start");
    require(bl_frame(engine, 16.6666666667) == BL_OK, "actual production ordered alpha draws");
    auto pixels = capture(color);
    std::ofstream image(output / (model.stem().string() + ".bgra"), std::ios::binary);
    image.write(reinterpret_cast<const char*>(pixels.data()),
                static_cast<std::streamsize>(pixels.size()));
    bl_EngineStats stats = {};
    require(bl_getEngineStats(engine, &stats) == BL_OK && stats.drawCallCount == count,
            "ordered state corresponds to actual submitted draws");
    state << "],\"draws\":" << stats.drawCallCount;
    require(bl_disposeRuntime(runtime) == BL_OK, "submitted GPU resources retired");
    bl_destroyAssetDecoder(decoder);
    require(!memory.live && !memory.bytes, "exact tracked runtime/provider teardown");
    state << ",\"trackedTeardownBytes\":0}\n";
    printf("%s: %zu actual draws, tracked teardown zero\n", model.stem().string().c_str(), count);
    fflush(stdout);
}

int main(int argc, char** argv)
{
    require(argc == 4, "fixtures, unchanged licensed lighting directory, new output directory");
    std::filesystem::path output(argv[3]);
    std::filesystem::create_directories(output);
    require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "image COM");
    HWND window = CreateWindowExW(0, L"STATIC", L"Native alpha GPU tests", WS_POPUP, 0, 0, 64, 64,
                                  nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    bgfx::Init init;
    init.type = bgfx::RendererType::Direct3D11;
    init.swapChain.nwh = window;
    init.swapChain.width = 64;
    init.swapChain.height = 64;
    init.swapChain.formatColor = bgfx::TextureFormat::BGRA8;
    init.swapChain.formatDepthStencil = bgfx::TextureFormat::D24S8;
    require(bgfx::init(init), "real native D3D11");
    bgfx::TextureHandle attachments[2] = {
        bgfx::createTexture2D(64, 64, false, 1, bgfx::TextureFormat::BGRA8, BGFX_TEXTURE_RT),
        bgfx::createTexture2D(64, 64, false, 1, bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT)};
    auto framebuffer = bgfx::createFrameBuffer(2, attachments, true);
    require(bgfx::isValid(framebuffer), "color/alpha plus depth framebuffer");
    Compiler compiler;
    compiler.output = output;
    const char* names[] = {"opaque-default-zero",
                           "opaque-factor-reference",
                           "opaque-image-zero",
                           "opaque-image-reference",
                           "mask-factor-zero",
                           "mask-factor-below",
                           "mask-factor-equal",
                           "mask-factor-above",
                           "mask-factor-one",
                           "blend-factor-zero",
                           "blend-factor-below",
                           "blend-factor-equal",
                           "blend-factor-above",
                           "blend-factor-one",
                           "mask-zero-cutoff",
                           "mask-above-one-cutoff",
                           "mask-custom-equal",
                           "mask-image-default",
                           "mask-image-equal",
                           "mask-image-factor-equal",
                           "mask-image-factor-below",
                           "blend-image-factor",
                           "mask-maps",
                           "opaque-maps-reference",
                           "blend-maps",
                           "mask-mirrored",
                           "mask-backface",
                           "opaque-mirrored-reference",
                           "opaque-backface-reference",
                           "overlap-near-first",
                           "overlap-far-first",
                           "overlap-reference-near",
                           "overlap-reference-far",
                           "blend-no-depth-write",
                           "mixed-phases",
                           "mixed-background-reference",
                           "opaque-depth-blocker",
                           "mask-instances",
                           "blend-instances",
                           "equal-depth-near-first",
                           "equal-depth-far-first",
                           "equal-depth-orders",
                           "mask-depth-pass",
                           "mask-depth-discard",
                           "blend-mirrored",
                           "blend-backface",
                           "mask-single-sided-front",
                           "mask-single-sided-back",
                           "blend-single-sided-front",
                           "blend-single-sided-back",
                           "opaque-single-sided-front",
                           "opaque-single-sided-back"};
    for (const char* name : names)
    {
        render(std::filesystem::path(argv[1]) / (std::string(name) + ".glb"), argv[2], output,
               framebuffer, attachments[0], compiler);
    }
    require(compiler.calls == compiler.releases && compiler.maskCotangent >= 3,
            "paired compiler release and actual derivative normal plus MASK compilation");
    std::ofstream result(output / "result.json");
    result << "{\"actualProductionGpuCases\":" << sizeof(names) / sizeof(names[0])
           << ",\"compilerCalls\":" << compiler.calls
           << ",\"compilerReleases\":" << compiler.releases
           << ",\"maskCotangentCompilations\":" << compiler.maskCotangent
           << ",\"modifiedShaderOutputs\":false,\"allTrackedTeardownBytes\":0}\n";
    bgfx::destroy(framebuffer);
    wait(nullptr);
    bgfx::shutdown();
    DestroyWindow(window);
    CoUninitialize();
}
