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
#include <iomanip>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

using Json = nlohmann::json;

static size_t assertions = 0;

static void require(bool value, const char* message)
{
    ++assertions;
    if (!value)
    {
        fprintf(stderr, "GPU geometry: %s\n", message);
        exit(2);
    }
}

struct Memory
{
    size_t live = 0;
    size_t bytes = 0;
    size_t calls = 0;
    size_t failAt = 0;
};

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
        memory->bytes += bytes;
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

static size_t retirements = 0;

static bl_Status wait(void*)
{
    const uint8_t marker[4] = {13, 31, 61, 255};
    uint8_t pixels[4] = {};
    auto source = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0,
                                       bgfx::copy(marker, 4));
    auto target = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8,
                                       BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST);
    require(bgfx::isValid(source) && bgfx::isValid(target), "real retirement fence resources");
    bgfx::TextureRegion from;
    bgfx::TextureRegion to;
    from.init(source);
    to.init(target);
    bgfx::blit(126, to, from);
    uint32_t ready = bgfx::read(to, pixels);
    uint32_t frame = bgfx::frame();
    while (static_cast<int32_t>(frame - ready) < 0)
    {
        frame = bgfx::frame();
    }
    require(!memcmp(pixels, marker, 4), "actual submitted work completed before retirement");
    ++retirements;
    bgfx::destroy(source);
    bgfx::destroy(target);
    return BL_OK;
}

static std::vector<uint8_t> file(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "fixture input exists");
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(input), {});
}

static std::vector<uint8_t> capture(bgfx::TextureHandle color)
{
    auto staging = bgfx::createTexture2D(64, 64, false, 1, bgfx::TextureFormat::BGRA8,
                                         BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST);
    require(bgfx::isValid(staging), "actual BGRA staging target");
    bgfx::TextureRegion from;
    bgfx::TextureRegion to;
    from.init(color);
    to.init(staging);
    bgfx::blit(120, to, from);
    std::vector<uint8_t> pixels(64 * 64 * 4);
    uint32_t ready = bgfx::read(to, pixels.data());
    uint32_t frame = bgfx::frame();
    while (static_cast<int32_t>(frame - ready) < 0)
    {
        frame = bgfx::frame();
    }
    bgfx::destroy(staging);
    return pixels;
}

struct Context
{
    Memory memory;
    bl_AssetDecoder* decoder = nullptr;
    bl_AssetService provider = {};
    bl_IOService io = SandboxAssetIO();
    bl_ShaderCompilerService2 compiler = bl_shaderCompilerService2();
    bl_Runtime* runtime = nullptr;
    bl_EngineContext engine = {};
    const bl_ParsedGltf* prefix = nullptr;
    size_t parses = 0;
    size_t parsedReleases = 0;
    size_t environments = 0;
    size_t environmentReleases = 0;
    size_t images = 0;
    size_t imageReleases = 0;
    size_t reads = 0;
    size_t readReleases = 0;
    size_t compiles = 0;
    size_t compiledReleases = 0;
    size_t colorReflections = 0;
    bl_Status imageFailure = BL_OK;
    bool imageEmpty = false;
    bool corruptPrefix = false;
    bool reentry = false;
    std::string probe;
    std::filesystem::path output;
};

static bl_Status parse(void* user, bl_Bytes bytes, bl_String base, bl_ParsedGltf* out)
{
    auto* context = static_cast<Context*>(user);
    require(out->structSize == sizeof(bl_ParsedGltf4) && out->version == 4,
            "Core passes actual full nested v4 storage");
    context->prefix = out;
    ++context->parses;
    if (context->reentry)
    {
        bl_AssetContainer sentinel = {context->runtime, UINT64_MAX};
        bl_AssetSource source = {BL_ASSET_SOURCE_BYTES, {}, bytes};
        require(bl_loadGltf(context->engine, &source, &sentinel) == BL_BUSY &&
                    sentinel._id == UINT64_MAX, "raw callback reentry rejected before IO");
    }
    auto status = context->provider.parseGltf(context->provider.userData, bytes, base, out);
    if (context->corruptPrefix)
    {
        out->structSize = sizeof(bl_ParsedGltf);
        out->version = 2;
    }
    return status;
}

static void releaseParsed(void* user, bl_ParsedGltf* out)
{
    auto* context = static_cast<Context*>(user);
    require(context->prefix == out, "exact same original prefix released");
    ++context->parsedReleases;
    context->provider.releaseGltf(context->provider.userData, out);
}

static bl_Status parseEnvironment(void* user, bl_Bytes bytes, bl_ParsedEnvironment* out)
{
    auto* context = static_cast<Context*>(user);
    require(out->structSize == sizeof(*out) && out->version == 2, "environment stays exact v2");
    ++context->environments;
    return context->provider.parseEnvironment(context->provider.userData, bytes, out);
}

static void releaseEnvironment(void* user, bl_ParsedEnvironment* out)
{
    auto* context = static_cast<Context*>(user);
    ++context->environmentReleases;
    context->provider.releaseEnvironment(context->provider.userData, out);
}

static bl_Status decode(void* user, bl_Bytes bytes, bl_HostImage* out)
{
    auto* context = static_cast<Context*>(user);
    ++context->images;
    if (context->imageEmpty && context->imageFailure != BL_OK)
    {
        return context->imageFailure;
    }
    auto status = context->io.decodeImage(context->io.userData, bytes, out);
    return status == BL_OK ? context->imageFailure : status;
}

static void releaseImage(void* user, bl_HostImage* out)
{
    auto* context = static_cast<Context*>(user);
    ++context->imageReleases;
    context->io.releaseImage(context->io.userData, out);
}

static bl_Status readFile(void* user, bl_String uri, bl_HostBuffer* out)
{
    auto* context = static_cast<Context*>(user);
    ++context->reads;
    return context->io.readFile(context->io.userData, uri, out);
}

static void releaseFile(void* user, bl_HostBuffer* out)
{
    auto* context = static_cast<Context*>(user);
    ++context->readReleases;
    context->io.releaseBuffer(context->io.userData, out);
}

static bl_Status compile(void* user, const bl_ShaderCompileRequest2* request,
                         bl_ShaderCompileResult2* result)
{
    auto* context = static_cast<Context*>(user);
    ++context->compiles;
    std::string fragment(request->base.fragment.wgsl.data, request->base.fragment.wgsl.length);
    std::string vertex(request->base.vertex.wgsl.data, request->base.vertex.wgsl.length);
    bool pbr = fragment.find("var baseColor=baseColorSample.rgb") != std::string::npos;
    if (pbr && !context->output.empty())
    {
        std::ofstream(context->output / ("vertex-" + std::to_string(context->compiles) + ".wgsl"))
            << vertex;
        std::ofstream(context->output / ("production-" + std::to_string(context->compiles) + ".wgsl"))
            << fragment;
    }
    if (pbr && !context->probe.empty())
    {
        std::string final = "return vec4f(color,alpha*material.materialAlpha);";
        auto position = fragment.find(final);
        if (position == std::string::npos)
        {
            final = "return vec4f(color,1.0);";
            position = fragment.find(final);
        }
        require(position != std::string::npos, "probe exact production source anchor");
        std::string replacement = context->probe == "normal"
            ? "return vec4f(N*0.5+0.5,1.0);"
            : context->probe == "geometric"
                ? "return vec4f(N_geom*0.5+0.5,1.0);"
                : "return input.vColor;";
        if (context->probe != "fetch" || fragment.find("input.vColor") != std::string::npos)
        {
            fragment.replace(position, final.size(), replacement);
        }
    }
    auto observed = *request;
    observed.base.fragment.wgsl = {fragment.data(), fragment.size()};
    auto status = context->compiler.compile(context->compiler.userData, &observed, result);
    if (status == BL_OK && pbr && context->probe.empty() &&
        vertex.find("@location(9)color") != std::string::npos)
    {
        bool found = false;
        for (size_t i = 0; i < result->base.attributeCount; ++i)
        {
            const auto& attribute = result->base.attributes[i];
            if (attribute.semantic == BL_ATTRIBUTE_COLOR)
            {
                require(attribute.location == 9 && attribute.components == 4,
                        "actual COLOR0 float4 reflection at input9");
                found = true;
            }
        }
        require(found, "actual color input is active in production IR");
        ++context->colorReflections;
    }
    return status;
}

static void releaseCompiled(void* user, bl_ShaderCompileResult2* result)
{
    auto* context = static_cast<Context*>(user);
    ++context->compiledReleases;
    context->compiler.release(context->compiler.userData, result);
}

static void setup(Context& context, bgfx::FrameBufferHandle framebuffer)
{
    bl_Allocator allocator = {&context.memory, allocate, release};
    require(bl_createAssetDecoder(&allocator, &context.decoder, &context.provider) == BL_OK &&
                bl_assetDecoderService4(context.decoder, &context.provider) == BL_OK,
            "raw v4 provider setup");
    auto assets = context.provider;
    assets.userData = &context;
    assets.parseGltf = parse;
    assets.releaseGltf = releaseParsed;
    assets.parseEnvironment = parseEnvironment;
    assets.releaseEnvironment = releaseEnvironment;
    auto io = context.io;
    io.userData = &context;
    io.readFile = readFile;
    io.releaseBuffer = releaseFile;
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
    options.assets = &assets;
    options.shaderCompiler2 = &shader;
    require(bl_createRuntime2(&options, &context.runtime) == BL_OK, "tracked native runtime");
    bl_NativeEngineOptions native = {};
    native.ownership = BL_BGFX_BORROWED;
    native.backend = BL_RENDERER_D3D11;
    native.target = {BL_TARGET_BGFX_FRAMEBUFFER, framebuffer.idx, 64, 64, BL_COLOR_BGRA8,
                     BL_DEPTH_D24S8, 1, 0, 116};
    native.isExternalBgfxInitialized = initialized;
    native.waitForSubmittedWork = wait;
    require(bl_createEngine(context.runtime, &native, nullptr, &context.engine) == BL_OK,
            "real framebuffer native engine");
}

static void finish(Context& context)
{
    context.memory.failAt = 0;
    require(bl_disposeRuntime(context.runtime) == BL_OK, "actual submitted resource disposal");
    bl_destroyAssetDecoder(context.decoder);
    require(context.parses == context.parsedReleases && context.images == context.imageReleases &&
                context.environments == context.environmentReleases &&
                context.reads == context.readReleases &&
                context.compiles == context.compiledReleases,
            "every invoked raw/IO/image/compiler result released once");
    require(!context.memory.live && !context.memory.bytes, "tracked teardown exactly0");
}

static bl_Status load(Context& context, const std::vector<uint8_t>& bytes, bl_AssetContainer* out)
{
    bl_AssetSource source = {BL_ASSET_SOURCE_BYTES, {}, {bytes.data(), bytes.size()}};
    return bl_loadGltf(context.engine, &source, out);
}

static Json geometry(Context& context, bl_AssetContainer asset, const Json& source)
{
    Json meshes = Json::array();
    auto* owner = reinterpret_cast<L_Asset*>(l_peek(context.runtime, asset._id));
    const char* names[6] = {"POSITION", "NORMAL", "TEXCOORD_0", "TEXCOORD_1", "TANGENT", "COLOR_0"};
    for (size_t i = 0; i < owner->nodeCount; ++i)
    {
        auto* record = l_peek(context.runtime, owner->nodes[i]._id);
        if (!record || record->kind != L_MESH)
        {
            continue;
        }
        auto* mesh = reinterpret_cast<L_Mesh*>(record);
        bl_Mat4 world = {};
        uint64_t worldVersion = 0;
        require(bl_getNodeWorldMatrix({context.runtime, mesh->node.record.id}, &world,
                                      &worldVersion) == BL_OK, "actual source world matrix");
        bl_String parentName = {};
        require(bl_getNodeName({context.runtime, mesh->node.parent->record.id}, &parentName) == BL_OK,
                "original source parent identity/name");
        std::string parent(parentName.data, parentName.length);
        size_t node = std::stoul(parent.substr(5));
        size_t index = source["document"]["nodes"][node]["mesh"].get<size_t>();
        const auto& expected = source["geometry"][index];
        const auto& native = mesh->geometry;
        if (source.contains("expectedInstanceMatrices"))
        {
            auto matrices = source["expectedInstanceMatrices"].get<std::vector<float>>();
            bl_ThinInstanceView instances = {};
            require(bl_getThinInstances({context.runtime, mesh->node.record.id}, &instances) == BL_OK &&
                        instances.count == 1 && instances.matrices.count == matrices.size() &&
                        !memcmp(instances.matrices.data, matrices.data(), matrices.size() * sizeof(float)),
                    "count1 strided instance TRS decodes to exact native instance matrix");
        }
        require(mesh->assetFlatNormal == expected["flatNormal"].get<bool>(),
                "source missing NORMAL, not resolved-zero values, controls private flat flag");
        for (unsigned stream = 0; stream < 6; ++stream)
        {
            bool present = expected["streams"].contains(names[stream]);
            require(bool(native.attributeMask & (1u << stream)) == present,
                    "actual native vertex-format presence");
            if (present)
            {
                auto values = expected["streams"][names[stream]].get<std::vector<float>>();
                require(!memcmp(native.streams[stream], values.data(), values.size() * sizeof(float)),
                        "all native CPU geometry/color/normal values match independent byte oracle");
                size_t components = values.size() / native.vertices;
                for (size_t vertex = 0; vertex < native.vertices; ++vertex)
                {
                    require(!memcmp(native.gpuVertices + vertex * native.stride + native.offsets[stream],
                                    values.data() + vertex * components, components * sizeof(float)),
                            "actual submitted interleaved staging stream/offset/stride");
                }
            }
        }
        auto indices = expected["indices"].get<std::vector<uint32_t>>();
        require(native.indexCount == indices.size() &&
                    !memcmp(native.indices, indices.data(), indices.size() * sizeof(uint32_t)),
                "exact final sparse/expanded triangle indices, degenerates and winding");
        std::vector<double> low(3, INFINITY);
        std::vector<double> high(3, -INFINITY);
        auto positions = expected["streams"]["POSITION"].get<std::vector<float>>();
        for (size_t vertex = 0; vertex < native.vertices; ++vertex)
        {
            for (unsigned j = 0; j < 3; ++j)
            {
                low[j] = std::min(low[j], double(positions[vertex * 3 + j]));
                high[j] = std::max(high[j], double(positions[vertex * 3 + j]));
            }
        }
        require(native.minimum.x == low[0] && native.minimum.y == low[1] &&
                    native.minimum.z == low[2] && native.maximum.x == high[0] &&
                    native.maximum.y == high[1] && native.maximum.z == high[2],
                "native bounds include complete final overlaid POSITION");
        meshes.push_back({{"sourceMesh", index}, {"vertices", native.vertices},
                          {"indices", native.indexCount}, {"stride", native.stride},
                          {"attributeMask", native.attributeMask}, {"flat", mesh->assetFlatNormal},
                          {"material", l_meshMaterial(mesh)._id},
                          {"worldMatrix", std::vector<double>(world.values, world.values + 16)},
                          {"minimum", low}, {"maximum", high}});
    }
    require(meshes.size() == source["geometry"].size(), "no primitive silently omitted");
    return meshes;
}

static void toggleColors(Context& context, L_Scene* scene, const Json& source)
{
    for (size_t i = 0; i < scene->memberCount; ++i)
    {
        if (scene->members[i]->record.kind != L_MESH)
        {
            continue;
        }
        auto* mesh = reinterpret_cast<L_Mesh*>(scene->members[i]);
        const auto& g = mesh->geometry;
        bl_MeshGeometry values = {};
        values.positions = {g.streams[0], g.vertices * 3};
        values.normals = {g.streams[1], g.vertices * 3};
        values.indices = {g.indices, g.indexCount};
        values.uvs = {g.streams[2], g.streams[2] ? g.vertices * 2 : 0};
        values.uvs2 = {g.streams[3], g.streams[3] ? g.vertices * 2 : 0};
        values.tangents = {g.streams[4], g.streams[4] ? g.vertices * 4 : 0};
        std::vector<float> colors;
        if (!g.streams[5])
        {
            colors.resize(g.vertices * 4);
            for (size_t vertex = 0; vertex < g.vertices; ++vertex)
            {
                for (unsigned j = 0; j < 4; ++j)
                {
                    colors[vertex * 4 + j] = source["color"][j].get<float>();
                }
            }
            values.colors = {colors.data(), colors.size()};
        }
        require(bl_resizeMeshGeometry(context.engine, {context.runtime, mesh->node.record.id},
                                       &values) == BL_OK,
                "generic full geometry update changes real color layout, preserving other streams");
    }
}

static void render(const std::filesystem::path& fixture, const std::filesystem::path& lighting,
                   const std::filesystem::path& output, const Json& source,
                   bgfx::FrameBufferHandle framebuffer, bgfx::TextureHandle color,
                   const std::string& probe)
{
    Context context;
    context.probe = probe;
    context.output = output / source["name"].get<std::string>();
    std::filesystem::create_directories(context.output);
    setup(context, framebuffer);
    auto bytes = file(fixture);
    bl_AssetContainer asset = {};
    require(load(context, bytes, &asset) == BL_OK, "real GLB geometry load");
    auto cpu = geometry(context, asset, source);
    bytes.assign(bytes.size(), 0xa3);
    bl_SceneContext scene = {};
    require(bl_createSceneContext(context.engine, &scene) == BL_OK &&
                bl_addAssetContainerToScene(scene, asset) == BL_OK, "source scene ownership");
    std::string env = (lighting / "environmentSpecular.env").string();
    std::string brdf = (lighting / "brdf-lut.png").string();
    bl_EnvironmentOptions options = {};
    options.brdfUrl = {brdf.data(), brdf.size()};
    options.skipGround = BL_BOOL_TRUE;
    options.skipSkybox = BL_BOOL_TRUE;
    bl_EnvironmentTextures environment = {};
    auto environmentStatus =
        bl_loadEnvironment(scene, {env.data(), env.size()}, &options, &environment);
    if (environmentStatus != BL_OK)
    {
        bl_Error error = {};
        bl_getLastError(context.runtime, &error);
        fprintf(stderr, "Environment status%d: %.*s\n", int(environmentStatus),
                int(error.message.length), error.message.data);
    }
    require(environmentStatus == BL_OK, "constant synthetic independent PBR pixel lighting");
    bl_FreeCamera camera = {};
    require(bl_createFreeCamera(context.runtime, {0, 0, -3}, {}, &camera) == BL_OK, "fixed camera");
    bl_SceneProperties properties = {{.2, .4, .6, .25}, camera, 0};
    require(bl_setSceneProperties(scene, &properties) == BL_OK && bl_registerScene(scene) == BL_OK,
            "actual runtime production variant preparation");
    auto* nativeScene = reinterpret_cast<L_Scene*>(l_peek(context.runtime, scene._id));
    size_t variants = 0;
    for (size_t i = 0; i < nativeScene->memberCount; ++i)
    {
        auto* node = nativeScene->members[i];
        if (node->record.kind != L_MESH)
        {
            continue;
        }
        auto* mesh = reinterpret_cast<L_Mesh*>(node);
        const auto& packet = nativeScene->pbrPackets[i];
        require(packet.colors == bool(mesh->geometry.attributeMask & (1u << BL_ATTRIBUTE_COLOR)) &&
                    packet.flatNormal == mesh->assetFlatNormal &&
                    (!mesh->assetFlatNormal || !packet.tangents),
                "actual per-mesh color/flat/authored tangent variant");
        variants |= (size_t(1) << ((packet.colors ? 1 : 0) | (packet.flatNormal ? 2 : 0)));
    }
    if (source.value("shared", false))
    {
        require(variants == 15, "one shared material really prepared all4 color/normal variants");
        auto identity = cpu[0]["material"].get<uint64_t>();
        for (const auto& mesh : cpu)
        {
            require(mesh["material"].get<uint64_t>() == identity, "shared original material identity");
        }
    }
    size_t sourceTransforms = 0;
    for (size_t i = 0; i < nativeScene->memberCount; ++i)
    {
        auto kind = nativeScene->members[i]->record.kind;
        require(kind == L_MESH || kind == L_NODE,
                "authored camera attachments never instantiate a native camera in the asset");
        sourceTransforms += kind == L_NODE;
    }
    require(bl_startEngine(context.engine, nullptr, nullptr) == BL_OK &&
                bl_frame(context.engine, 16.6666666667) == BL_OK, "actual submitted geometry draw");
    auto pixels = capture(color);
    std::ofstream image(output / (source["name"].get<std::string>() + ".bgra"), std::ios::binary);
    image.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    bl_EngineStats stats = {};
    require(bl_getEngineStats(context.engine, &stats) == BL_OK && stats.drawCallCount == cpu.size(),
            "actual submitted draws match every source primitive");
    Json state = {{"cpuGeometryOracleByteExact", true}, {"meshes", cpu},
                  {"draws", stats.drawCallCount}, {"variantMask", variants}, {"probe", probe},
                  {"productionFragmentOutputModified", !probe.empty()},
                  {"actualColorReflections", context.colorReflections}};
    state["assetSceneMembership"] = nativeScene->memberCount;
    state["assetTransformMembership"] = sourceTransforms;
    if (source.value("shared", false))
    {
        for (const char* phase : {"updated", "restored"})
        {
            toggleColors(context, nativeScene, source);
            require(bl_frame(context.engine, 16.6666666667) == BL_OK,
                    "actual color-layout update recompiles shared material per-mesh variants");
            for (size_t i = 0; i < nativeScene->memberCount; ++i)
            {
                if (nativeScene->members[i]->record.kind == L_MESH)
                {
                    auto* mesh = reinterpret_cast<L_Mesh*>(nativeScene->members[i]);
                    require(nativeScene->pbrPackets[i].colors ==
                                bool(mesh->geometry.attributeMask & (1u << BL_ATTRIBUTE_COLOR)),
                            "no first-mesh or stale color variant after geometry layout mutation");
                }
            }
            auto changed = capture(color);
            std::ofstream updated(output / (source["name"].get<std::string>() + "-" + phase + ".bgra"),
                                  std::ios::binary);
            updated.write(reinterpret_cast<const char*>(changed.data()),
                           static_cast<std::streamsize>(changed.size()));
            if (std::string(phase) == "restored")
            {
                require(changed == pixels, "restored source color layouts restore exact GPU pixels");
            }
        }
        state["colorLayoutToggleAndRestorationActualGpu"] = true;
    }
    auto* owner = reinterpret_cast<L_Asset*>(l_peek(context.runtime, asset._id));
    bl_Mesh stale = {};
    for (size_t i = 0; i < owner->nodeCount; ++i)
    {
        auto* record = l_peek(context.runtime, owner->nodes[i]._id);
        if (record->kind == L_MESH)
        {
            stale = {context.runtime, record->id};
            break;
        }
    }
    require(bl_removeAssetContainerFromScene(scene, asset) == BL_OK &&
                bl_disposeAssetContainer(asset) == BL_OK, "last-owner actual GPU retirement");
    bl_MeshProperties staleProperties = {};
    require(bl_getMeshProperties(stale, &staleProperties) == BL_DISPOSED,
            "old mesh identity cannot resurrect");
    finish(context);
    state["trackedTeardownBytes"] = 0;
    state["compilerCalls"] = context.compiles;
    state["compilerReleases"] = context.compiledReleases;
    std::ofstream(output / (source["name"].get<std::string>() + ".json")) << state.dump(2) << '\n';
    printf("%s: %zu draws, exact CPU oracle, variant%zu, teardown0\n",
            source["name"].get<std::string>().c_str(), cpu.size(), variants);
}

static Json faults(const std::filesystem::path& fixture, bgfx::FrameBufferHandle framebuffer)
{
    auto bytes = file(fixture);
    size_t oom = 0;
    for (size_t fault = 1; fault < 512; ++fault)
    {
        Context context;
        setup(context, framebuffer);
        context.memory.failAt = context.memory.calls + fault;
        bl_AssetContainer out = {context.runtime, UINT64_MAX};
        auto status = load(context, bytes, &out);
        require(status == BL_OK || status == BL_OUT_OF_MEMORY, "each tracked sparse/color OOM propagates");
        if (status != BL_OK)
        {
            require(out._id == UINT64_MAX, "OOM publishes no partial asset");
        }
        finish(context);
        if (status == BL_OK)
        {
            break;
        }
        ++oom;
    }
    size_t imageFaults = 0;
    for (bool empty : {false, true})
    {
        for (auto status : {BL_CANCELLED, BL_HOST_ERROR, BL_OUT_OF_MEMORY})
        {
            Context context;
            setup(context, framebuffer);
            context.imageFailure = status;
            context.imageEmpty = empty;
            bl_AssetContainer out = {context.runtime, UINT64_MAX};
            require(load(context, bytes, &out) == status && out._id == UINT64_MAX,
                    "partial/empty actual image failure preserves output");
            finish(context);
            ++imageFaults;
        }
    }
    {
        Context context;
        setup(context, framebuffer);
        context.corruptPrefix = true;
        bl_AssetContainer out = {context.runtime, UINT64_MAX};
        require(load(context, bytes, &out) == BL_INVALID_ARGUMENT && out._id == UINT64_MAX,
                "corrupted returned prefix never exposes nested tails");
        finish(context);
    }
    {
        Context context;
        setup(context, framebuffer);
        context.reentry = true;
        size_t warmBytes = 0;
        size_t warmSlots = 0;
        for (unsigned i = 0; i < 32; ++i)
        {
            bl_AssetContainer asset = {};
            require(load(context, bytes, &asset) == BL_OK &&
                        bl_disposeAssetContainer(asset) == BL_OK, "sparse/color native owner churn");
            if (i == 4)
            {
                warmBytes = context.memory.bytes;
                warmSlots = context.runtime->count;
            }
            if (i > 4)
            {
                require(context.memory.bytes == warmBytes && context.runtime->count == warmSlots,
                        "native allocation/identity plateau after warmup");
            }
        }
        finish(context);
    }
    return {{"trackedOomPositions", oom}, {"partialEmptyImageFaults", imageFaults},
            {"corruptedPrefixReleasedOnce", true}, {"nativeChurnIterations", 32},
            {"nativeAllocationAndSlotPlateau", true}, {"allTrackedTeardownBytes", 0}};
}

int main(int argc, char** argv)
{
    require(argc == 4 || argc == 5, "fixtures, lighting, output, optional normal/geometric/fetch probe");
    std::filesystem::path fixtures(argv[1]);
    std::filesystem::path lighting(argv[2]);
    std::filesystem::path output(argv[3]);
    std::string probe = argc == 5 ? argv[4] : "";
    std::filesystem::create_directories(output);
    auto manifest = Json::parse(std::ifstream(fixtures / "geometry-cases.json"));
    require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "image COM");
    HWND window = CreateWindowExW(0, L"STATIC", L"Geometry GPU tests", WS_POPUP, 0, 0, 64, 64,
                                  nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    bgfx::Init init;
    init.type = bgfx::RendererType::Direct3D11;
    init.swapChain.nwh = window;
    init.swapChain.width = 64;
    init.swapChain.height = 64;
    init.swapChain.formatColor = bgfx::TextureFormat::BGRA8;
    init.swapChain.formatDepthStencil = bgfx::TextureFormat::D24S8;
    require(bgfx::init(init), "actual native D3D11");
    bgfx::TextureHandle attachments[2] = {
        bgfx::createTexture2D(64, 64, false, 1, bgfx::TextureFormat::BGRA8, BGFX_TEXTURE_RT),
        bgfx::createTexture2D(64, 64, false, 1, bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT)};
    auto framebuffer = bgfx::createFrameBuffer(2, attachments, true);
    require(bgfx::isValid(framebuffer), "actual RGBA/depth framebuffer");
    size_t rendered = 0;
    size_t rejected = 0;
    for (const auto& source : manifest["cases"])
    {
        std::string name = source["name"].get<std::string>();
        if (source["expected"] != "BL_OK")
        {
            if (!probe.empty())
            {
                continue;
            }
            Context context;
            setup(context, framebuffer);
            bl_AssetContainer out = {context.runtime, UINT64_MAX};
            auto bytes = file(fixtures / (name + ".glb"));
            auto status = load(context, bytes, &out);
            require(status == (source["expected"] == "BL_UNSUPPORTED" ? BL_UNSUPPORTED : BL_INVALID_ARGUMENT) &&
                        out._id == UINT64_MAX, "labeled malformed/unsupported input fails atomically");
            finish(context);
            ++rejected;
            continue;
        }
        if (!probe.empty() &&
            ((probe == "normal" || probe == "geometric") ? !source.value("flatProbe", false)
                                                       : !(source.value("pixelOracle", false) ||
                                                           source.value("fetchProbe", false))))
        {
            continue;
        }
        render(fixtures / (name + ".glb"), lighting, output, source, framebuffer, attachments[0], probe);
        ++rendered;
    }
    Json report = {{"actualGpuCases", rendered}, {"malformedCases", rejected},
                   {"assertions", assertions}, {"probe", probe},
                   {"productionOutputModified", !probe.empty()}, {"actualGpuRetirements", retirements}};
    if (probe.empty() && !manifest.value("cameraIgnorePolicy", false))
    {
        report["faults"] = faults(fixtures / "sparse-zero-u32.glb", framebuffer);
    }
    std::ofstream(output / "result.json") << report.dump(2) << '\n';
    bgfx::destroy(framebuffer);
    wait(nullptr);
    bgfx::shutdown();
    DestroyWindow(window);
    CoUninitialize();
    printf("Geometry actual GPU %zu cases, %zu malformed; %zu assertions; all teardown0\n",
            rendered, rejected, assertions);
}
