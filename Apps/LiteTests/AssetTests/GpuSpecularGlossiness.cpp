#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include "AssetInternal.h"
#include "PbrInternal.h"
#include <babylon_lite_asset_decoder.h>
#include <babylon_lite_shader_compiler.h>
#include "../LiteSandbox/AssetIO.h"
#include <json.hpp>
#include <filesystem>
#include <fstream>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

using Json = nlohmann::json;
static size_t assertions = 0;
static size_t fences = 0;

static void require(bool value, const char* message)
{
    ++assertions;
    if (!value)
    {
        fprintf(stderr, "SpecGloss GPU: %s\n", message);
        exit(2);
    }
}

struct Context
{
    size_t live = 0;
    size_t bytes = 0;
    size_t calls = 0;
    size_t failAt = 0;
    size_t images = 0;
    size_t imageReleases = 0;
    size_t parses = 0;
    size_t parseReleases = 0;
    size_t compiles = 0;
    size_t compileReleases = 0;
    size_t reflections = 0;
    bl_Status imageFailure = BL_OK;
    bl_Status compileFailure = BL_OK;
    bool imageEmpty = false;
    bool malformedImage = false;
    bool malformedReflection = false;
    bool malformedPrefix = false;
    bool reentry = false;
    bl_Status rawFailure = BL_OK;
    bool observe = true;
    bl_AssetDecoder* decoder = nullptr;
    bl_AssetService provider = {};
    bl_ShaderCompilerService2 compiler = bl_shaderCompilerService2();
    bl_IOService io = SandboxAssetIO();
    bl_Runtime* runtime = nullptr;
    bl_EngineContext engine = {};
    const bl_ParsedGltf* prefix = nullptr;
    std::filesystem::path output;
    std::vector<std::string> productionFragments;
};

static void* allocate(void* user, size_t bytes, size_t alignment)
{
    auto* context = static_cast<Context*>(user);
    if (++context->calls == context->failAt)
    {
        return nullptr;
    }
    void* pointer = _aligned_malloc(bytes, alignment);
    if (pointer)
    {
        ++context->live;
        context->bytes += bytes;
    }
    return pointer;
}

static void deallocate(void* user, void* pointer, size_t bytes, size_t)
{
    if (pointer)
    {
        auto* context = static_cast<Context*>(user);
        --context->live;
        context->bytes -= bytes;
        _aligned_free(pointer);
    }
}

static std::vector<uint8_t> readGpu(bgfx::TextureHandle source, uint16_t width,
                                   uint16_t height, uint8_t mip = 0,
                                   bgfx::TextureFormat::Enum format = bgfx::TextureFormat::RGBA8)
{
    auto target = bgfx::createTexture2D(width, height, false, 1, format,
                                        BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST);
    require(bgfx::isValid(target), "actual GPU staging image");
    bgfx::TextureRegion from;
    bgfx::TextureRegion to;
    from.init(source);
    from.mip = mip;
    from.width = width;
    from.height = height;
    to.init(target);
    bgfx::blit(120, to, from);
    std::vector<uint8_t> bytes(size_t(width) * height * 4);
    uint32_t ready = bgfx::read(to, bytes.data());
    uint32_t frame = bgfx::frame();
    while (static_cast<int32_t>(frame - ready) < 0)
    {
        frame = bgfx::frame();
    }
    bgfx::destroy(target);
    return bytes;
}

static bl_Status wait(void*)
{
    const uint8_t marker[4] = {17, 43, 73, 255};
    auto source = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8,
                                        0, bgfx::copy(marker, 4));
    require(bgfx::isValid(source), "submitted fence source");
    auto result = readGpu(source, 1, 1);
    require(!memcmp(marker, result.data(), 4), "actual submitted work completes before retirement");
    ++fences;
    bgfx::destroy(source);
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
    if (context->reentry)
    {
        bl_AssetSource source = {BL_ASSET_SOURCE_BYTES, {}, bytes};
        bl_AssetContainer unchanged = {context->runtime, UINT64_MAX};
        require(bl_loadGltf(context->engine, &source, &unchanged) == BL_BUSY &&
                    unchanged._id == UINT64_MAX, "service5 callback reentry rejected before IO");
    }
    auto status = context->provider.parseGltf(context->provider.userData, bytes, base, out);
    if (context->malformedPrefix)
    {
        out->version = 99;
    }
    return status == BL_OK ? context->rawFailure : status;
}

static void releaseParsed(void* user, bl_ParsedGltf* out)
{
    auto* context = static_cast<Context*>(user);
    require(out == context->prefix, "same caller-owned raw5 prefix released once");
    ++context->parseReleases;
    context->provider.releaseGltf(context->provider.userData, out);
}

static bl_Status decode(void* user, bl_Bytes bytes, bl_HostImage* out)
{
    auto* context = static_cast<Context*>(user);
    ++context->images;
    if (context->imageEmpty)
    {
        return context->imageFailure;
    }
    auto status = context->io.decodeImage(context->io.userData, bytes, out);
    if (context->malformedImage)
    {
        out->rowStrideBytes = SIZE_MAX;
    }
    return status == BL_OK ? context->imageFailure : status;
}

static void releaseImage(void* user, bl_HostImage* out)
{
    auto* context = static_cast<Context*>(user);
    ++context->imageReleases;
    context->io.releaseImage(context->io.userData, out);
}

static bl_Status compile(void* user, const bl_ShaderCompileRequest2* request,
                         bl_ShaderCompileResult2* result)
{
    auto* context = static_cast<Context*>(user);
    ++context->compiles;
    std::string fragment(request->base.fragment.wgsl.data, request->base.fragment.wgsl.length);
    context->productionFragments.push_back(fragment);
    auto status = context->compiler.compile(context->compiler.userData, request, result);
    bool specGloss = fragment.find("specularGlossinessFactors") != std::string::npos;
    if (status == BL_OK && specGloss)
    {
        require(fragment.find("ormTexture") == std::string::npos &&
                    fragment.find("var metallic=") == std::string::npos,
                "genuine production SG has no MR map or metallic conversion");
        bool factors = false;
        for (size_t i = 0; i < result->base.uniformCount; ++i)
        {
            const auto& uniform = result->base.uniforms[i];
            if (l_name(uniform.name, "specularGlossinessFactors"))
            {
                require(uniform.type == BL_UNIFORM_VEC4 && uniform.byteOffset == 64 &&
                            uniform.byteSize == 16 &&
                            result->base.uniformBlocks[uniform.blockIndex].byteSize == 80,
                        "production SG Float32 vec4 factor reflection, exact80-byte block");
                factors = true;
            }
        }
        require(factors, "factor-only and mapped workflow both reflect real SG factors");
        ++context->reflections;
        if (context->observe)
        {
            std::ofstream(context->output /
                          ("production-" + std::to_string(context->compiles) + ".wgsl")) << fragment;
        }
        if (context->malformedReflection)
        {
            auto* blocks = const_cast<bl_ReflectedUniformBlock*>(result->base.uniformBlocks);
            for (size_t i = 0; i < result->base.uniformBlockCount; ++i)
            {
                if (blocks[i].byteSize == 80)
                {
                    blocks[i].byteSize = 79;
                }
            }
        }
        if (context->compileFailure != BL_OK)
        {
            return context->compileFailure;
        }
    }
    return status;
}

static void releaseCompiled(void* user, bl_ShaderCompileResult2* result)
{
    auto* context = static_cast<Context*>(user);
    ++context->compileReleases;
    context->compiler.release(context->compiler.userData, result);
}

static void setup(Context& context, bgfx::FrameBufferHandle framebuffer)
{
    bl_Allocator allocator = {&context, allocate, deallocate};
    require(bl_createAssetDecoder(&allocator, &context.decoder, &context.provider) == BL_OK &&
                bl_assetDecoderService5(context.decoder, &context.provider) == BL_OK,
            "actual raw5 provider");
    auto service = context.provider;
    service.userData = &context;
    service.parseGltf = parse;
    service.releaseGltf = releaseParsed;
    auto io = context.io;
    // Environment callbacks use provider userdata, not the GLB wrapper's context.
    service.parseEnvironment = nullptr;
    service.releaseEnvironment = nullptr;
    service.parseEnvironment = [](void* user, bl_Bytes bytes, bl_ParsedEnvironment* out)
    {
        auto* owner = static_cast<Context*>(user);
        return owner->provider.parseEnvironment(owner->provider.userData, bytes, out);
    };
    service.releaseEnvironment = [](void* user, bl_ParsedEnvironment* out)
    {
        auto* owner = static_cast<Context*>(user);
        owner->provider.releaseEnvironment(owner->provider.userData, out);
    };
    io.userData = &context;
    io.readFile = [](void* user, bl_String uri, bl_HostBuffer* out)
    {
        auto* owner = static_cast<Context*>(user);
        return owner->io.readFile(owner->io.userData, uri, out);
    };
    io.releaseBuffer = [](void* user, bl_HostBuffer* out)
    {
        auto* owner = static_cast<Context*>(user);
        owner->io.releaseBuffer(owner->io.userData, out);
    };
    io.decodeImage = decode;
    io.releaseImage = releaseImage;
    auto shader = context.compiler;
    shader.userData = &context;
    shader.compile = compile;
    shader.release = releaseCompiled;
    bl_RuntimeOptions2 options = {};
    options.structSize = sizeof(options);
    options.version = 2;
    options.base.allocator = allocator;
    options.base.io = &io;
    options.assets = &service;
    options.shaderCompiler2 = &shader;
    require(bl_createRuntime2(&options, &context.runtime) == BL_OK, "tracked GPU runtime");
    bl_NativeEngineOptions native = {};
    native.ownership = BL_BGFX_BORROWED;
    native.backend = BL_RENDERER_D3D11;
    native.target = {BL_TARGET_BGFX_FRAMEBUFFER, framebuffer.idx, 64, 64, BL_COLOR_BGRA8,
                     BL_DEPTH_D24S8, 1, 0, 116};
    native.isExternalBgfxInitialized = initialized;
    native.waitForSubmittedWork = wait;
    auto engineStatus = bl_createEngine(context.runtime, &native, nullptr, &context.engine);
    if (engineStatus != BL_OK)
    {
        bl_Error error = {};
        bl_getLastError(context.runtime, &error);
        fprintf(stderr, "Synthetic engine setup status%d: %.*s\n", int(engineStatus),
                int(error.message.length), error.message.data);
    }
    require(engineStatus == BL_OK, "native framebuffer engine");
}

static void finish(Context& context)
{
    context.failAt = 0;
    require(bl_disposeRuntime(context.runtime) == BL_OK, "native submitted retirement");
    bl_destroyAssetDecoder(context.decoder);
    require(!context.live && !context.bytes && context.parses == context.parseReleases &&
                context.images == context.imageReleases &&
                context.compiles == context.compileReleases,
            "all tracked bytes0 and parse/image/compiler calls released once");
}

static std::vector<uint8_t> file(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "synthetic fixture exists");
    return {std::istreambuf_iterator<char>(input), {}};
}

static bl_Status load(Context& context, const std::vector<uint8_t>& bytes, bl_AssetContainer* out)
{
    bl_AssetSource source = {BL_ASSET_SOURCE_BYTES, {}, {bytes.data(), bytes.size()}};
    return bl_loadGltf(context.engine, &source, out);
}

static bl_Status prepare(Context& context, bl_AssetContainer asset,
                         const std::filesystem::path& lighting, bl_SceneContext* scene,
                         size_t pipelineFault = 0)
{
    require(bl_createSceneContext(context.engine, scene) == BL_OK &&
                bl_addAssetContainerToScene(*scene, asset) == BL_OK, "source scene ownership");
    auto env = (lighting / "constant.env").string();
    auto brdf = (lighting / "brdf-constant.png").string();
    bl_EnvironmentOptions options = {};
    options.brdfUrl = {brdf.data(), brdf.size()};
    options.skipGround = BL_BOOL_TRUE;
    options.skipSkybox = BL_BOOL_TRUE;
    bl_EnvironmentTextures environment = {};
    require(bl_loadEnvironment(*scene, {env.data(), env.size()}, &options, &environment) == BL_OK,
            "actual synthetic RGBD cube and independent BRDF input");
    bl_FreeCamera camera = {};
    require(bl_createFreeCamera(context.runtime, {0, 0, -3}, {0, 0, 0}, &camera) == BL_OK,
            "fixed production camera");
    bl_SceneProperties properties = {{.2, .4, .6, .25}, camera, 0};
    require(bl_setSceneProperties(*scene, &properties) == BL_OK, "native scene clear");
    if (pipelineFault)
    {
        context.failAt = context.calls + pipelineFault;
    }
    return bl_registerScene(*scene);
}

static void resources(Context& context, bl_AssetContainer asset)
{
    auto* owner = reinterpret_cast<L_Asset*>(l_peek(context.runtime, asset._id));
    for (size_t i = 1; i < owner->materialCount; ++i)
    {
        auto* material = reinterpret_cast<L_Material*>(l_peek(context.runtime,
                                                             owner->materials[i]._id));
        if (!material || !material->pbrSpecularGlossiness)
        {
            continue;
        }
        bl_PbrMaterialInfo info = {};
        memset(&info, 0x72, sizeof(info));
        auto previous = info;
        require(bl_getPbrMaterialInfo({context.runtime, material->record.id}, &info) == BL_UNSUPPORTED &&
                    !memcmp(&previous, &info, sizeof(info)), "real SG getter never invents MR data");
        require(L_NULL(material->pbrInfo.ormTexture), "inactive MR resource not constructed");
        bl_Texture2D handles[5] = {material->pbrInfo.baseColorTexture,
            material->pbrSpecularGlossinessTexture, material->pbrNormalTexture,
            material->pbrOcclusionTexture, material->pbrEmissiveTexture};
        for (unsigned role = 0; role < 5; ++role)
        {
            if (L_NULL(handles[role]))
            {
                continue;
            }
            auto* texture = reinterpret_cast<L_Texture*>(l_peek(context.runtime, handles[role]._id));
            require(texture->info.srgb == (role < 2 || role == 4),
                    "physical role-specific sRGB RGB/linear alpha resource");
            size_t offset = 0;
            for (uint8_t mip = 0; mip < texture->mipCount; ++mip)
            {
                uint16_t width = static_cast<uint16_t>(texture->info.width >> mip);
                uint16_t height = static_cast<uint16_t>(texture->info.height >> mip);
                width = width ? width : 1;
                height = height ? height : 1;
                auto bytes = readGpu({texture->handleIndex}, width, height, mip);
                if (texture->mipPixels)
                {
                    require(!memcmp(bytes.data(), texture->mipPixels + offset, bytes.size()),
                            "all actual native mip bytes match complete linear-light chain");
                }
                offset += bytes.size();
            }
        }
    }
}

static std::vector<uint8_t> render(Context& context, bl_AssetContainer asset,
                                   const std::filesystem::path& lighting,
                                   bgfx::TextureHandle color)
{
    bl_SceneContext scene = {};
    require(prepare(context, asset, lighting, &scene) == BL_OK, "real production SG compilation");
    auto* native = reinterpret_cast<L_Scene*>(l_peek(context.runtime, scene._id));
    size_t meshes = 0;
    for (size_t i = 0; i < native->memberCount; ++i)
    {
        if (native->members[i]->record.kind != L_MESH)
        {
            continue;
        }
        auto* mesh = reinterpret_cast<L_Mesh*>(native->members[i]);
        const auto& packet = native->pbrPackets[i];
        require(packet.colors == bool(mesh->geometry.attributeMask & (1u << BL_ATTRIBUTE_COLOR)) &&
                    packet.flatNormal == mesh->assetFlatNormal &&
                    packet.instances == mesh->hasInstances &&
                    (!packet.flatNormal || !packet.tangents),
                "real per-mesh color/flat/tangent/instance variants, not first-mesh state");
        ++meshes;
    }
    require(bl_startEngine(context.engine, nullptr, nullptr) == BL_OK &&
                bl_frame(context.engine, 16.6666666667) == BL_OK, "actual native IBL/RGBA draws");
    bl_EngineStats stats = {};
    require(bl_getEngineStats(context.engine, &stats) == BL_OK && stats.drawCallCount == meshes,
            "actual submitted source primitive count");
    auto pixels = readGpu(color, 64, 64, 0, bgfx::TextureFormat::BGRA8);
    std::ofstream(context.output / "capture.bgra", std::ios::binary).write(
        reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    Json materials = Json::array();
    auto* owner = reinterpret_cast<L_Asset*>(l_peek(context.runtime, asset._id));
    for (size_t i = 1; i < owner->materialCount; ++i)
    {
        auto* material = reinterpret_cast<L_Material*>(l_peek(context.runtime, owner->materials[i]._id));
        if (material)
        {
            materials.push_back({{"specGloss", material->pbrSpecularGlossiness},
                {"diffuseFactor", {material->pbrInfo.baseColorFactor.r,
                    material->pbrInfo.baseColorFactor.g, material->pbrInfo.baseColorFactor.b,
                    material->pbrInfo.baseColorFactor.a}},
                {"specularFactor", {material->pbrSpecularFactor.x,
                    material->pbrSpecularFactor.y, material->pbrSpecularFactor.z}},
                {"glossinessFactor", material->pbrGlossinessFactor},
                {"baseImage", material->pbrBaseImage}, {"blend", material->blending},
                {"mask", material->testing}, {"cutoff", material->pbrAlphaCutoff}});
        }
    }
    std::ofstream(context.output / "state.json") << Json({{"draws", meshes},
        {"materials", materials}, {"productionWGSLModified", false},
        {"sgReflectionCount", context.reflections}}).dump(2) << '\n';
    require(bl_disposeAssetContainer(asset) == BL_BUSY, "scene-owned source cannot dispose");
    return pixels;
}

static Json faults(const std::vector<uint8_t>& bytes, bgfx::FrameBufferHandle framebuffer,
                    const std::filesystem::path& output, const std::filesystem::path& lighting)
{
    size_t oom = 0;
    for (size_t position = 1; ; ++position)
    {
        Context context;
        setup(context, framebuffer);
        context.failAt = context.calls + position;
        bl_AssetContainer out = {context.runtime, UINT64_MAX};
        auto status = load(context, bytes, &out);
        if (status == BL_OK)
        {
            require(bl_disposeAssetContainer(out) == BL_OK, "complete allocation sweep success");
            finish(context);
            break;
        }
        require(status == BL_OUT_OF_MEMORY && out._id == UINT64_MAX,
                "every Core/provider tracked allocation fault is atomic OOM");
        finish(context);
        ++oom;
    }
    for (bool empty : {false, true})
    {
        for (auto status : {BL_CANCELLED, BL_HOST_ERROR, BL_OUT_OF_MEMORY, BL_NOT_READY})
        {
            Context context;
            setup(context, framebuffer);
            context.imageFailure = status;
            context.imageEmpty = empty;
            bl_AssetContainer out = {context.runtime, UINT64_MAX};
            require(load(context, bytes, &out) == status && out._id == UINT64_MAX,
                    "authored map failure never becomes a unit input fallback");
            finish(context);
        }
        for (auto status : {BL_CANCELLED, BL_HOST_ERROR, BL_OUT_OF_MEMORY, BL_NOT_READY})
        {
            Context context;
            setup(context, framebuffer);
            context.rawFailure = status;
            bl_AssetContainer out = {context.runtime, UINT64_MAX};
            require(load(context, bytes, &out) == status && out._id == UINT64_MAX,
                    "partial service5 result propagates status and releases full source ownership");
            finish(context);
        }
        {
            Context context;
            setup(context, framebuffer);
            context.malformedPrefix = true;
            bl_AssetContainer out = {context.runtime, UINT64_MAX};
            require(load(context, bytes, &out) == BL_INVALID_ARGUMENT && out._id == UINT64_MAX,
                    "altered returned v5 prefix rejected before tail consumption");
            finish(context);
        }
    }
    {
        Context context;
        setup(context, framebuffer);
        context.malformedImage = true;
        bl_AssetContainer out = {context.runtime, UINT64_MAX};
        require(load(context, bytes, &out) == BL_INVALID_ARGUMENT && out._id == UINT64_MAX,
                "malformed image stride rejected before copying");
        finish(context);
    }
    for (bool reflection : {false, true})
    {
        Context context;
        context.output = output;
        setup(context, framebuffer);
        bl_AssetContainer asset = {};
        require(load(context, bytes, &asset) == BL_OK, "compiler-fault source load");
        context.compileFailure = reflection ? BL_OK : BL_HOST_ERROR;
        context.malformedReflection = reflection;
        bl_SceneContext scene = {};
        require(prepare(context, asset, lighting, &scene) ==
                    (reflection ? BL_SHADER_ERROR : BL_HOST_ERROR),
                "actual partial compiler result/bad reflection rejected and released once");
        finish(context);
    }
    size_t pipelineOom = 0;
    for (size_t position = 1; ; ++position)
    {
        Context context;
        context.output = output / ("pipeline-fault-" + std::to_string(position));
        std::filesystem::create_directories(context.output);
        setup(context, framebuffer);
        bl_AssetContainer asset = {};
        require(load(context, bytes, &asset) == BL_OK, "pipeline-fault source ownership");
        bl_SceneContext scene = {};
        auto status = prepare(context, asset, lighting, &scene, position);
        require(status == BL_OK || status == BL_OUT_OF_MEMORY,
                "every production source/pipeline/reflection allocation fault is explicit OOM");
        finish(context);
        if (status == BL_OK)
        {
            break;
        }
        ++pipelineOom;
    }
    {
        Context context;
        setup(context, framebuffer);
        context.reentry = true;
        size_t warmBytes = 0;
        size_t warmSlots = 0;
        for (unsigned i = 0; i < 250; ++i)
        {
            bl_AssetContainer asset = {};
            require(load(context, bytes, &asset) == BL_OK &&
                        bl_disposeAssetContainer(asset) == BL_OK, "SG native material/map churn");
            if (i == 4)
            {
                warmBytes = context.bytes;
                warmSlots = context.runtime->count;
            }
            if (i > 4)
            {
                require(context.bytes == warmBytes && context.runtime->count == warmSlots,
                        "native allocations/identities plateau after warmup");
            }
        }
        finish(context);
    }
    return {{"trackedOomPositions", oom}, {"pipelineOomPositions", pipelineOom},
            {"imageFailures", 9},
            {"productionCompilerFailures", 2}, {"churnIterations", 250},
            {"trackedTeardownBytes", 0}};
}

static void sharingUpdates(const std::vector<uint8_t>& bytes,
                           bgfx::FrameBufferHandle framebuffer)
{
    Context context;
    setup(context, framebuffer);
    bl_AssetContainer asset = {};
    require(load(context, bytes, &asset) == BL_OK, "actual shared SG role asset");
    auto* owner = reinterpret_cast<L_Asset*>(l_peek(context.runtime, asset._id));
    auto* material = reinterpret_cast<L_Material*>(l_peek(context.runtime,
                                                         owner->materials[1]._id));
    auto srgb = material->pbrInfo.baseColorTexture;
    auto linear = material->pbrNormalTexture;
    require(srgb._id == material->pbrSpecularGlossinessTexture._id &&
                srgb._id == material->pbrEmissiveTexture._id &&
                linear._id == material->pbrOcclusionTexture._id && linear._id != srgb._id,
            "diffuse/SG/emissive share sRGB; normal/AO independently share linear resource");
    bl_ParsedGltf5 parsed = {};
    auto* prefix = &parsed.document4.document3.document;
    prefix->structSize = sizeof(parsed);
    prefix->version = 5;
    require(context.provider.parseGltf(context.provider.userData, {bytes.data(), bytes.size()},
                                       {}, prefix) == BL_OK, "raw supplement for second owner");
    bl_Material second = {};
    require(l_createPbrFactorMaterial(context.engine, prefix->materials, &second,
                                      parsed.specularGlossinessInputs) == BL_OK &&
                l_applyAssetMaterialInputs(owner, &parsed.document4.document3, 0, second,
                                            &parsed.document4,
                                            parsed.specularGlossinessInputs) == BL_OK,
            "second SG material references the same cache by every role");
    context.provider.releaseGltf(context.provider.userData, prefix);
    auto* copied = reinterpret_cast<L_Material*>(l_peek(context.runtime, second._id));
    require(copied->pbrSpecularGlossinessTexture._id == srgb._id &&
                copied->pbrNormalTexture._id == linear._id,
            "shared identities, no role-created duplicate resources");
    auto* srgbRecord = reinterpret_cast<L_Texture*>(l_peek(context.runtime, srgb._id));
    auto* linearRecord = reinterpret_cast<L_Texture*>(l_peek(context.runtime, linear._id));
    require(srgbRecord->references == 6 && linearRecord->references == 4,
            "references count material roles, not unique handle");
    auto previousLinear = readGpu({linearRecord->handleIndex}, 2, 2);
    auto originalSrgb = readGpu({srgbRecord->handleIndex}, 2, 2);
    const uint8_t changed[4] = {41, 79, 113, 17};
    require(bl_updateTexture2DFromPixels(context.engine, srgb, {changed, 4}, 0, 0, 1, 1) == BL_OK,
            "existing API updates real SG/diffuse shared RGB and linear alpha");
    auto physical = readGpu({srgbRecord->handleIndex}, 2, 2);
    require(!memcmp(physical.data(), changed, 4) &&
                readGpu({linearRecord->handleIndex}, 2, 2) == previousLinear,
            "actual GPU update, linear-role resource remains independent");
    double average[4] = {};
    for (unsigned channel = 0; channel < 4; ++channel)
    {
        for (unsigned pixel = 0; pixel < 4; ++pixel)
        {
            double value = physical[pixel * 4 + channel] / 255.0;
            if (channel < 3)
            {
                value = value <= .04045 ? value / 12.92 : pow((value + .055) / 1.055, 2.4);
            }
            average[channel] += value / 4;
        }
        if (channel < 3)
        {
            average[channel] = average[channel] <= .0031308 ? average[channel] * 12.92
                : 1.055 * pow(average[channel], 1 / 2.4) - .055;
        }
    }
    auto mip = readGpu({srgbRecord->handleIndex}, 1, 1, 1);
    for (unsigned channel = 0; channel < 4; ++channel)
    {
        require(mip[channel] == uint8_t(floor(average[channel] * 255 + .5)),
                "updated physical mip independently averages linear-light RGB/linear gloss alpha");
    }
    require(bl_updateTexture2DFromPixels(context.engine, srgb,
                {originalSrgb.data(), originalSrgb.size()}, 0, 0, 2, 2) == BL_OK &&
                readGpu({srgbRecord->handleIndex}, 2, 2) == originalSrgb,
            "full texture restoration is byte exact");
    bl_SceneContext firstScene = {};
    bl_SceneContext secondScene = {};
    require(bl_createSceneContext(context.engine, &firstScene) == BL_OK &&
                bl_createSceneContext(context.engine, &secondScene) == BL_OK &&
                bl_addAssetContainerToScene(firstScene, asset) == BL_OK &&
                bl_addAssetContainerToScene(secondScene, asset) == BL_OK &&
                bl_removeAssetContainerFromScene(firstScene, asset) == BL_OK &&
                bl_disposeAssetContainer(asset) == BL_BUSY && !srgbRecord->record.disposed,
            "removing first owning scene retains second scene and shared material resources");
    require(bl_removeAssetContainerFromScene(secondScene, asset) == BL_OK &&
                bl_disposeAssetContainer(asset) == BL_OK &&
                !srgbRecord->record.disposed && srgbRecord->references == 3,
            "container disposal retains another SG material owner");
    require(bl_disposePbrMaterial({context.runtime, second._id}) == BL_OK,
            "last role owner disposal retires real GPU resources");
    finish(context);
}

int main(int argc, char** argv)
{
    require(argc == 3, "synthetic fixture directory and new ignored output directory");
    std::filesystem::path fixtures(argv[1]);
    std::filesystem::path output(argv[2]);
    std::filesystem::create_directories(output);
    require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "native image COM");
    HWND window = CreateWindowExW(0, L"STATIC", L"SpecGloss GPU", WS_POPUP, 0, 0, 64, 64,
                                  nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    bgfx::Init init;
    init.type = bgfx::RendererType::Direct3D11;
    init.swapChain.nwh = window;
    init.swapChain.width = 64;
    init.swapChain.height = 64;
    init.swapChain.formatColor = bgfx::TextureFormat::BGRA8;
    init.swapChain.formatDepthStencil = bgfx::TextureFormat::D24S8;
    require(bgfx::init(init), "actual unchanged D3D11 production backend");
    bgfx::TextureHandle textures[2] = {
        bgfx::createTexture2D(64, 64, false, 1, bgfx::TextureFormat::BGRA8, BGFX_TEXTURE_RT),
        bgfx::createTexture2D(64, 64, false, 1, bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT)};
    auto framebuffer = bgfx::createFrameBuffer(2, textures, true);
    require(bgfx::isValid(framebuffer), "actual color/depth framebuffer");
    auto manifest = Json::parse(std::ifstream(fixtures / "fixture-manifest.json"));
    size_t rendered = 0;
    size_t rejected = 0;
    std::vector<uint8_t> referencePixels;
    std::vector<std::string> referenceFragments;
    for (const auto& source : manifest["fixtures"])
    {
        auto name = source["id"].get<std::string>();
        Context context;
        context.output = output / name;
        std::filesystem::create_directories(context.output);
        setup(context, framebuffer);
        auto bytes = file(fixtures / source["file"].get<std::string>());
        bl_AssetContainer asset = {context.runtime, UINT64_MAX};
        auto status = load(context, bytes, &asset);
        bool valid = source["expectedProvider5"] == "BL_OK" && source["expectedCore5"] == "BL_OK";
        if (!valid)
        {
            auto label = source["expectedProvider5"] == "BL_OK" ? source["expectedCore5"]
                                                               : source["expectedProvider5"];
            require(status == (label == "BL_INVALID_ARGUMENT" ? BL_INVALID_ARGUMENT : BL_UNSUPPORTED) &&
                        asset._id == UINT64_MAX, "synthetic unsupported/malformed atomic rejection");
            ++rejected;
        }
        else
        {
            require(status == BL_OK, "actual SG source loading");
            bytes.assign(bytes.size(), 0xb5);
            resources(context, asset);
            auto pixels = render(context, asset, fixtures / "lighting", textures[0]);
            if (name == "mapped-factors")
            {
                referencePixels = pixels;
                referenceFragments = context.productionFragments;
            }
            ++rendered;
        }
        finish(context);
    }
    {
        Context independent;
        independent.observe = false;
        independent.output = output / "probe-independent";
        std::filesystem::create_directories(independent.output);
        setup(independent, framebuffer);
        auto original = file(fixtures / "mapped-factors.glb");
        bl_AssetContainer repeated = {};
        require(load(independent, original, &repeated) == BL_OK &&
                    render(independent, repeated, fixtures / "lighting", textures[0]) ==
                        referencePixels &&
                    independent.productionFragments == referenceFragments,
                "fresh production compilation/pixels independent of diagnostic observer");
        finish(independent);
    }
    sharingUpdates(file(fixtures / "same-texture-all-roles.glb"), framebuffer);
    auto report = faults(file(fixtures / "common-material-roles.glb"), framebuffer,
                          output, fixtures / "lighting");
    report["actualRenderedCases"] = rendered;
    report["actualRejectedCases"] = rejected;
    report["assertions"] = assertions;
    report["actualGpuFences"] = fences;
    report["productionWGSLModified"] = false;
    report["actualSharingMipUpdateAndMultiOwnerRetirement"] = true;
    std::ofstream(output / "result.json") << report.dump(2) << '\n';
    bgfx::destroy(framebuffer);
    wait(nullptr);
    bgfx::shutdown();
    DestroyWindow(window);
    CoUninitialize();
    printf("SpecGloss GPU %zu rendered/%zu rejected, %zu assertions, %zu fences, teardown0\n",
           rendered, rejected, assertions, fences);
}
