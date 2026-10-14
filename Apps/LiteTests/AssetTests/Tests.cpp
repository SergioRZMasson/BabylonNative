#include "AssetInternal.h"
#include "PbrInternal.h"
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

struct Allocations
{
    size_t live;
    size_t calls;
    size_t failAt;
};

static void require(bool condition, const char* message)
{
    if (!condition)
    {
        fprintf(stderr, "Asset contract: %s\n", message);
        exit(2);
    }
}

static void* allocate(void* user, size_t bytes, size_t alignment)
{
    Allocations* state = (Allocations*)user;
    if (++state->calls == state->failAt)
    {
        return NULL;
    }
    void* result = _aligned_malloc(bytes, alignment);
    if (result)
    {
        ++state->live;
    }
    return result;
}

static void release(void* user, void* pointer, size_t, size_t)
{
    if (pointer)
    {
        --((Allocations*)user)->live;
        _aligned_free(pointer);
    }
}

struct Provider
{
    bl_Runtime* runtime;
    bl_EngineContext engine;
    bl_Status status;
    bl_Status readStatus;
    unsigned parseCalls;
    unsigned releases;
    unsigned reads;
    unsigned bufferReleases;
    bool reentry;
    bool malformed;
    bl_AssetBuffer buffer;
    bool version3;
    bool badPrefix;
    const bl_ParsedGltf* originalPrefix;
};

static bl_Status parse(void* user, bl_Bytes, bl_String, bl_ParsedGltf* out)
{
    Provider* provider = (Provider*)user;
    ++provider->parseCalls;
    provider->originalPrefix = out;
    if (provider->version3)
    {
        require(out->structSize == sizeof(bl_ParsedGltf3) &&
                    out->version == BL_ASSET_DOCUMENT_VERSION_3,
                "advertised service3 receives actual full caller-owned storage");
    }
    if (provider->reentry)
    {
        bl_RuntimeOptions2 options = {};
        options.structSize = sizeof(options);
        options.version = 2;
        bl_Runtime* result = provider->runtime;
        require(bl_createRuntime2(&options, &result) == BL_BUSY, "provider runtime reentry");
        bl_AssetContainer container = {};
        const uint8_t data[4] = {};
        bl_AssetSource source = {BL_ASSET_SOURCE_BYTES, {}, {data, 4}};
        require(bl_loadGltf(provider->engine, &source, &container) == BL_BUSY,
                "provider loader reentry");
    }
    out->gltfVersion = 2;
    out->buffers = &provider->buffer;
    out->bufferCount = provider->malformed ? SIZE_MAX : 1;
    out->diagnostic = {155, {"copied provider diagnostic", 26}};
    if (provider->badPrefix)
    {
        out->structSize = sizeof(bl_ParsedGltf);
        out->version = BL_ASSET_DOCUMENT_VERSION;
    }
    return provider->status;
}

static void releaseParsed(void* user, bl_ParsedGltf* out)
{
    require(((Provider*)user)->originalPrefix == out, "release receives same original prefix");
    ++((Provider*)user)->releases;
    *out = {};
}

static bl_Status parseEnvironment(void*, bl_Bytes, bl_ParsedEnvironment*)
{
    return BL_CANCELLED;
}

static void releaseEnvironment(void*, bl_ParsedEnvironment*) {}

static bl_Status read(void* user, bl_String, bl_HostBuffer* out)
{
    Provider* provider = (Provider*)user;
    ++provider->reads;
    out->data = provider->buffer.bytes.data;
    out->byteCount = provider->buffer.bytes.count;
    return provider->readStatus;
}

static void releaseBuffer(void* user, bl_HostBuffer* out)
{
    ++((Provider*)user)->bufferReleases;
    *out = {};
}

static bl_Status wait(void*)
{
    return BL_OK;
}

static bl_Runtime* runtime(Allocations* state, Provider* provider = NULL)
{
    bl_RuntimeOptions2 options = {};
    options.structSize = sizeof(options);
    options.version = 2;
    options.base.allocator = {state, allocate, release};
    bl_AssetService service = {sizeof(service), provider && provider->version3 ? 3u : 2u,
                              provider, parse, releaseParsed,
                              parseEnvironment, releaseEnvironment};
    bl_IOService io = {};
    io.userData = provider;
    io.readFile = read;
    io.releaseBuffer = releaseBuffer;
    if (provider)
    {
        options.assets = &service;
        options.base.io = &io;
    }
    bl_Runtime* result = NULL;
    require(bl_createRuntime2(&options, &result) == BL_OK, "runtime create");
    if (provider)
    {
        provider->runtime = result;
    }
    return result;
}

static L_Engine* engine(bl_Runtime* runtime)
{
    L_Record* record = NULL;
    require(l_record(runtime, sizeof(L_Engine), L_ENGINE, 1, NULL, &record) == BL_OK,
            "data-only engine fixture");
    L_Engine* engine = (L_Engine*)record;
    engine->runtime = runtime;
    engine->native.ownership = BL_BGFX_BORROWED;
    engine->native.waitForSubmittedWork = wait;
    return engine;
}

static void hierarchy()
{
    Allocations allocations = {};
    bl_Runtime* r = runtime(&allocations);
    const uint8_t bytes[4] = {};
    bl_AssetBuffer buffer = {{bytes, 4}, 4, {}};
    bl_ParsedGltf data = {};
    data.structSize = sizeof(data);
    data.version = 2;
    data.gltfVersion = 2;
    data.buffers = &buffer;
    data.bufferCount = 1;
    require(l_validateAsset(&data) == BL_OK, "minimal raw document");
    data.nodeCount = SIZE_MAX;
    require(l_validateAsset(&data) == BL_INVALID_ARGUMENT, "huge top-level count");
    data.nodeCount = 0;
    data.unsupportedFeatures = UINT64_C(1) << 63;
    require(l_validateAsset(&data) == BL_INVALID_ARGUMENT, "unknown feature bit");
    data.unsupportedFeatures = BL_ASSET_UNSUPPORTED_EXTENSION;
    require(l_validateAsset(&data) == BL_UNSUPPORTED, "known unsupported feature bit");
    data.unsupportedFeatures = 0;
    bl_AssetNode nodes[3] = {};
    uint32_t children[3] = {1, 2, 0};
    data.nodes = nodes;
    data.nodeCount = 3;
    nodes[0].children = children;
    nodes[0].childCount = 1;
    nodes[1].children = children + 1;
    nodes[1].childCount = 1;
    require(l_validateAsset(&data) == BL_OK &&
                l_validateAssetHierarchy(r, &data) == BL_OK, "acyclic chain");
    nodes[2].children = children + 2;
    nodes[2].childCount = 1;
    require(l_validateAssetHierarchy(r, &data) == BL_INVALID_ARGUMENT, "raw cycle");
    nodes[2].children = children + 1;
    require(l_validateAssetHierarchy(r, &data) == BL_INVALID_ARGUMENT, "raw multi-parent");
    nodes[2] = {};
    nodes[0].childCount = 2;
    children[1] = 1;
    require(l_validateAssetHierarchy(r, &data) == BL_INVALID_ARGUMENT, "duplicate raw child");
    nodes[0] = {};
    nodes[1] = {};
    const uint8_t invalid[2] = {0xc0, 0xaf};
    nodes[0].hasName = true;
    nodes[0].name = {(const char*)invalid, 2};
    require(l_validateAsset(&data) == BL_INVALID_ARGUMENT, "raw UTF8");
    nodes[0] = {};
    nodes[0].childCount = SIZE_MAX;
    require(l_validateAsset(&data) == BL_INVALID_ARGUMENT, "huge child span");
    nodes[0] = {};
    bl_AssetPbrDefinition factor = {};
    factor.hasEmissiveFactor = true;
    factor.emissiveFactor.x = NAN;
    data.materials = &factor;
    data.materialCount = 1;
    require(l_validateAsset(&data) == BL_INVALID_ARGUMENT, "nonfinite raw factor before feature dispatch");
    factor.emissiveFactor.x = 1;
    require(l_validateAsset(&data) == BL_OK, "finite emissive factor in legacy raw document");
    for (unsigned mode = 0; mode <= BL_ASSET_ALPHA_BLEND; ++mode)
    {
        factor.hasAlphaMode = true;
        factor.alphaMode = (bl_AssetAlphaMode)mode;
        factor.hasAlphaCutoff = true;
        factor.alphaCutoff = 0.5;
        factor.hasBaseColorFactor = true;
        factor.baseColorFactor = {1, 1, 1, 0.4999};
        require(l_validateAsset(&data) == BL_OK, "all core alpha modes in legacy raw document");
        factor.alphaCutoff = NAN;
        require(l_validateAsset(&data) == BL_INVALID_ARGUMENT, "nonfinite raw alpha cutoff");
        factor.alphaCutoff = -0.001;
        require(l_validateAsset(&data) == BL_INVALID_ARGUMENT, "negative raw alpha cutoff");
        factor.alphaCutoff = 1.5;
        require(l_validateAsset(&data) == BL_OK, "cutoff above one is valid and rejects all");
        factor.baseColorFactor.a = 1.001;
        require(l_validateAsset(&data) == BL_INVALID_ARGUMENT, "raw alpha factor above one");
        factor.baseColorFactor.a = -0.001;
        require(l_validateAsset(&data) == BL_INVALID_ARGUMENT, "negative raw alpha factor");
    }
    factor.hasBaseColorFactor = false;
    factor.hasAlphaCutoff = false;
    factor.alphaMode = (bl_AssetAlphaMode)3;
    require(l_validateAsset(&data) == BL_INVALID_ARGUMENT, "unknown raw alpha enum");
    factor.hasAlphaMode = false;
    require(l_validateAsset(&data) == BL_OK, "absent alpha fields ignore unused storage");
    data.materials = NULL;
    data.materialCount = 0;
    nodes[0].hasMatrix = true;
    l_identity(&nodes[0].matrix);
    nodes[0].matrix.values[0] = 0;
    nodes[0].matrix.values[1] = 1;
    nodes[0].matrix.values[4] = -1;
    nodes[0].matrix.values[5] = 0;
    nodes[0].matrix.values[12] = 7;
    bl_TransformNodeOptions trs = {};
    require(l_composeAssetMatrix(nodes, &trs) == BL_OK && trs.position.x == 0 &&
                trs.scaling.x == 1 && trs.rotationQuaternion.w == 1,
            "matrix locks raw source matrix without invented decomposition");
    nodes[0].matrix.values[0] = -1;
    nodes[0].matrix.values[4] = 0.25;
    require(l_composeAssetMatrix(nodes, &trs) == BL_OK,
            "raw affine mirrored/sheared matrix retained without decomposition");
    nodes[0].hasMatrix = false;
    nodes[0].hasScale = true;
    nodes[0].scale = {1, -1, 1};
    require(l_composeAssetMatrix(nodes, &trs) == BL_OK && trs.scaling.y == -1,
            "ordinary mirrored TRS preserves original scale");
    std::vector<bl_AssetNode> deep(20000);
    std::vector<uint32_t> edges(deep.size());
    for (size_t i = 0; i + 1 < deep.size(); ++i)
    {
        edges[i] = (uint32_t)i + 1;
        deep[i].children = edges.data() + i;
        deep[i].childCount = 1;
    }
    data.nodes = deep.data();
    data.nodeCount = deep.size();
    require(l_validateAsset(&data) == BL_OK &&
                l_validateAssetHierarchy(r, &data) == BL_OK,
            "deep raw hierarchy validates without recursive stack overflow");
    require(bl_disposeRuntime(r) == BL_OK && !allocations.live, "hierarchy teardown");
}

static void accessors()
{
    alignas(16) uint8_t binary[96] = {};
    bl_AssetBuffer buffer = {{binary,80},80,{}};
    bl_AssetBufferView views[3] = {{0,0,36,{}},{0,36,36,{}},{0,72,6,{}}};
    bl_AssetAccessor inputs[3] = {};
    for (uint32_t i=0;i<3;++i)
    {
        inputs[i].bufferView={true,i};
        inputs[i].count=3;
        inputs[i].componentType=i==2?BL_ASSET_COMPONENT_U16:BL_ASSET_COMPONENT_F32;
        inputs[i].shape=i==2?BL_ASSET_SCALAR:BL_ASSET_VEC3;
    }
    bl_AssetAttribute attributes[2] = {{{"POSITION",8},0},{{"NORMAL",6},1}};
    bl_AssetPrimitive primitive = {};
    primitive.attributes=attributes;
    primitive.attributeCount=2;
    primitive.indices={true,2};
    bl_AssetMesh mesh = {};
    mesh.primitives=&primitive;
    mesh.primitiveCount=1;
    bl_ParsedGltf data = {};
    data.structSize=sizeof(data);
    data.version=2;data.gltfVersion=2;
    data.buffers=&buffer;data.bufferCount=1;
    data.bufferViews=views;data.bufferViewCount=3;
    data.accessors=inputs;data.accessorCount=3;
    data.meshes=&mesh;data.meshCount=1;
    require(l_validateAsset(&data)==BL_OK,"valid raw accessor fixture");
    inputs[0].byteOffset=4;
    require(l_validateAsset(&data)==BL_INVALID_ARGUMENT,"last-element view overrun");
    inputs[0].byteOffset=0;
    inputs[0].count=SIZE_MAX;
    require(l_validateAsset(&data)==BL_INVALID_ARGUMENT,"huge accessor count before access");
    inputs[0].count=3;
    views[0].byteOffset=SIZE_MAX;
    require(l_validateAsset(&data)==BL_INVALID_ARGUMENT,"raw view overflow");
    views[0].byteOffset=0;
    buffer.bytes.data=binary+1;
    require(l_validateAsset(&data)==BL_INVALID_ARGUMENT,"provider binary base alignment independent of offsets");
    buffer.bytes.data=binary;
    views[0].byteStride={true,16};
    require(l_validateAsset(&data)==BL_INVALID_ARGUMENT,"strided last-element view overrun");
    views[0].byteStride={};
    attributes[1].semantic={"POSITION",8};
    require(l_validateAsset(&data)==BL_INVALID_ARGUMENT,"duplicate attribute semantic");
    attributes[1].semantic={"NORMAL",6};
    inputs[0].sparsePresent=true;
    require(l_validateAsset(&data)==BL_UNSUPPORTED,"sparse flag not omitted");
    inputs[0].sparsePresent=false;
    primitive.indices={};
    primitive.attributeCount=1;
    require(l_validateAsset(&data)==BL_OK,"nonindexed triangles allow generated normals");
    inputs[0].count=2;
    require(l_validateAsset(&data)==BL_INVALID_ARGUMENT,"nonindexed triangle count");
    inputs[0].count=3;
    bl_AssetBufferView interleaved[2]={{0,0,84,{true,28}},{0,84,6,{}}};
    buffer.bytes={binary,sizeof(binary)};buffer.declaredByteLength=sizeof(binary);
    data.bufferViews=interleaved;data.bufferViewCount=2;
    bl_AssetAccessor varied[4]={};
    for(uint32_t i=0;i<3;++i)
    {
        varied[i].bufferView={true,0};varied[i].byteOffset=i*12;varied[i].count=3;
        varied[i].componentType=i==2?BL_ASSET_COMPONENT_U16:BL_ASSET_COMPONENT_F32;
        varied[i].shape=i==2?BL_ASSET_VEC2:BL_ASSET_VEC3;varied[i].normalized=i==2;
    }
    varied[3].bufferView={true,1};varied[3].count=3;
    varied[3].componentType=BL_ASSET_COMPONENT_U16;varied[3].shape=BL_ASSET_SCALAR;
    bl_AssetAttribute all[3]={{{"POSITION",8},0},{{"NORMAL",6},1},{{"TEXCOORD_0",10},2}};
    data.accessors=varied;data.accessorCount=4;
    primitive.attributes=all;primitive.attributeCount=3;primitive.indices={true,3};
    require(l_validateAsset(&data)==BL_OK,"interleaved positions normals normalized U16 UVs");
    interleaved[0].byteLength=83;
    require(l_validateAsset(&data)==BL_INVALID_ARGUMENT,"strided normalized final UV overrun");
    interleaved[0].byteLength=84;
    interleaved[0].byteStride.value=SIZE_MAX;
    require(l_validateAsset(&data)==BL_INVALID_ARGUMENT,"huge stride before pointer access");
    interleaved[0].byteStride.value=28;
    varied[2].componentType=BL_ASSET_COMPONENT_F32;
    require(l_validateAsset(&data)==BL_UNSUPPORTED,"normalized FLOAT cannot be reinterpreted");
}

static void partialAdd()
{
    Allocations allocations = {};
    bl_Runtime* r = runtime(&allocations);
    L_Engine* e = engine(r);
    bl_EngineContext eh = {r, e->record.id};
    bl_SceneContext sh = {};
    require(bl_createSceneContext(eh, &sh) == BL_OK, "scene create");
    bl_TransformNode root = {};
    bl_TransformNode first = {};
    bl_TransformNode last = {};
    require(bl_createTransformNode(r, {"root", 4}, NULL, &root) == BL_OK &&
                bl_createTransformNode(r, {"first", 5}, NULL, &first) == BL_OK &&
                bl_createTransformNode(r, {"last", 4}, NULL, &last) == BL_OK,
            "container hierarchy nodes");
    require(bl_appendNodeChild(root, first) == BL_OK &&
                bl_appendNodeChild(root, last) == BL_OK, "late child fixture");
    l_cleanNode(r, (L_Node*)l_peek(r, last._id));
    l_peek(r, last._id)->disposed = true;
    L_Record* record = NULL;
    require(l_record(r, sizeof(L_Asset), L_ASSET, 45, NULL, &record) == BL_OK,
            "container fixture");
    L_Asset* asset = (L_Asset*)record;
    asset->engine = e;
    asset->root = root;
    bl_AssetContainer container = {r, record->id};
    require(bl_addAssetContainerToScene(sh, container) == BL_DISPOSED,
            "late child reports disposed");
    L_Scene* scene = (L_Scene*)l_peek(r, sh._id);
    require(scene->memberCount == 2 && scene->members[0]->record.id == root._id &&
                scene->members[1]->record.id == first._id &&
                scene->members[1]->parent == scene->members[0],
            "earlier entities remain added in original source order");
    require(bl_addAssetContainerToScene(sh, container) == BL_DISPOSED &&
                scene->memberCount == 4 && scene->members[0]->sceneCount == 1,
            "duplicate partial additions retain one owner per scene");
    require(bl_removeAssetContainerFromScene(sh, container) == BL_OK &&
                scene->memberCount == 0, "partial remove");
    bl_NodeChildren childrenView = {};
    require(bl_getNodeChildren(root, &childrenView) == BL_OK && childrenView.count == 2,
            "remove retains original children array");
    require(bl_disposeScene(sh) == BL_OK && bl_disposeRuntime(r) == BL_OK &&
                !allocations.live, "partial add teardown");
}

static void releasesAndFaults()
{
    for (size_t fault = 0; fault < 40; ++fault)
    {
        Allocations allocations = {};
        Provider provider = {};
        const uint8_t bytes[4] = {};
        provider.buffer = {{bytes, 4}, 4, {}};
        bl_Runtime* r = runtime(&allocations, &provider);
        L_Engine* e = engine(r);
        provider.engine = {r, e->record.id};
        provider.reentry = true;
        bl_AssetSource source = {BL_ASSET_SOURCE_URI, {"memory.glb", 10}, {}};
        const bl_AssetContainer sentinel = {r, UINT64_MAX};
        bl_AssetContainer container = sentinel;
        size_t before = allocations.calls;
        allocations.failAt = fault ? before + fault : 0;
        bl_Status status = bl_loadGltf(provider.engine, &source, &container);
        allocations.failAt = 0;
        require(provider.parseCalls == 1 && provider.releases == 1 &&
                    provider.reads == 1 && provider.bufferReleases == 1,
                "exactly one release per successful callback invocation");
        if (status == BL_OK)
        {
            require(bl_disposeAssetContainer(container) == BL_OK, "empty successful asset release");
        }
        else
        {
            require(status == BL_OUT_OF_MEMORY && container._id == sentinel._id,
                    "allocation failure output unchanged");
        }
        require(bl_disposeRuntime(r) == BL_OK && !allocations.live, "fault teardown leak");
        if (fault && status == BL_OK)
        {
            printf("Core asset allocations: %zu failure positions\n", fault - 1);
            break;
        }
    }
    for (bl_Status expected : {BL_CANCELLED, BL_NOT_READY, BL_HOST_ERROR, BL_INVALID_ARGUMENT})
    {
        Allocations allocations = {};
        Provider provider = {};
        const uint8_t bytes[4] = {};
        provider.buffer = {{bytes, 4}, 4, {}};
        provider.status = expected;
        bl_Runtime* r = runtime(&allocations, &provider);
        L_Engine* e = engine(r);
        provider.engine = {r, e->record.id};
        bl_AssetSource source = {BL_ASSET_SOURCE_BYTES, {}, {bytes, 4}};
        bl_AssetContainer container = {r, UINT64_MAX};
        require(bl_loadGltf(provider.engine, &source, &container) == expected &&
                    container._id == UINT64_MAX && provider.releases == 1,
                "parse partial/cancel/error propagation and release");
        bl_Error diagnostic = {};
        require(bl_getLastError(r, &diagnostic) == BL_OK &&
                    diagnostic.sourceErrorCode == 155 &&
                    diagnostic.message.length == 26,
                "diagnostics copied before destructive release");
        provider.status = BL_OK;
        provider.malformed = true;
        require(bl_loadGltf(provider.engine, &source, &container) == BL_INVALID_ARGUMENT &&
                    provider.releases == 2, "malformed service output paired release");
        source = {BL_ASSET_SOURCE_URI, {"bad.glb", 7}, {}};
        provider.readStatus = expected;
        require(bl_loadGltf(provider.engine, &source, &container) == expected &&
                    provider.parseCalls == 2 && provider.bufferReleases == 1,
                "failed/partial IO release without parse invocation");
        require(bl_disposeRuntime(r) == BL_OK && !allocations.live, "service error teardown");
    }
}

static void textureProtocol3()
{
    alignas(16) uint8_t bytes[16] = {};
    bl_AssetBuffer buffer = {{bytes, sizeof(bytes)}, sizeof(bytes), {}};
    bl_AssetBufferView view = {0, 0, sizeof(bytes), {}};
    bl_AssetImage image = {};
    image.bufferView = {true, 0};
    image.hasMimeType = true;
    image.mimeType = {"image/png", 9};
    bl_AssetTexture texture = {{true, 0}, {true, 0}, 0};
    bl_AssetSampler sampler = {};
    bl_AssetPbrDefinition material = {};
    material.textureInfoFields = BL_ASSET_TEXTURE_BASE_COLOR;
    bl_AssetMaterialInputs inputs = {};
    inputs.baseColor.present = true;
    bl_ParsedGltf3 data = {};
    data.document.structSize = sizeof(data);
    data.document.version = BL_ASSET_DOCUMENT_VERSION_3;
    data.document.gltfVersion = 2;
    data.document.buffers = &buffer;
    data.document.bufferCount = 1;
    data.document.bufferViews = &view;
    data.document.bufferViewCount = 1;
    data.document.materials = &material;
    data.document.materialCount = 1;
    data.document.imageCount = 1;
    data.document.textureCount = 1;
    data.document.samplerCount = 1;
    data.images = &image;
    data.textures = &texture;
    data.samplers = &sampler;
    data.materialInputs = &inputs;
    const char* reason = NULL;
    require(l_validateAsset(&data.document, &reason, &data) == BL_OK,
            "full raw image/material protocol3 descriptor");
    require(l_validateAsset(&data.document) == BL_INVALID_ARGUMENT,
            "version2 storage path never reinterpreted as extended");
    data.document.imageCount = SIZE_MAX;
    require(l_validateAsset(&data.document, &reason, &data) == BL_INVALID_ARGUMENT,
            "huge extended count rejected before image access");
    data.document.imageCount = 1;
    data.images = (const bl_AssetImage*)((const uint8_t*)&image + 1);
    require(l_validateAsset(&data.document, &reason, &data) == BL_INVALID_ARGUMENT,
            "misaligned extended array");
    data.images = &image;
    image.hasUri = true; image.uri = {"x.png", 5};
    require(l_validateAsset(&data.document, &reason, &data) == BL_INVALID_ARGUMENT,
            "image URI/bufferView mutually exclusive");
    image.bufferView.present = false;
    require(l_validateAsset(&data.document, &reason, &data) == BL_UNSUPPORTED && reason,
            "external image URI actionable unsupported");
    image.hasUri = false; image.bufferView.present = true;
    image.mimeType = {"image/webp", 10};
    require(l_validateAsset(&data.document, &reason, &data) == BL_UNSUPPORTED,
            "unsupported image MIME");
    image.mimeType = {"image/jpeg", 10};
    require(l_validateAsset(&data.document, &reason, &data) == BL_OK, "JPEG image declaration");
    sampler.magFilter = {true, 9987};
    require(l_validateAsset(&data.document, &reason, &data) == BL_INVALID_ARGUMENT,
            "invalid magnification filter");
    sampler.magFilter = {true, 9729};
    sampler.minFilter = {true, 9987};
    sampler.wrapS = {true, 33071};
    sampler.wrapT = {true, 33648};
    require(l_validateAsset(&data.document, &reason, &data) == BL_OK,
            "linear mip and clamp/mirror sampler fields");
    inputs.baseColor.texCoord = {true, 2};
    require(l_validateAsset(&data.document, &reason, &data) == BL_UNSUPPORTED,
            "unsupported UV channel not silently zero");
    inputs.baseColor.texCoord = {true, 1};
    require(l_validateAsset(&data.document, &reason, &data) == BL_OK, "UV1 declaration retained");
    inputs.normal.present = true;
    material.textureInfoFields |= BL_ASSET_TEXTURE_NORMAL;
    inputs.normal.hasScale = true;
    inputs.normal.scale = 0.375;
    inputs.normal.texCoord = {true, 1};
    inputs.occlusion.present = true;
    inputs.occlusion.hasStrength = true;
    inputs.occlusion.strength = 0.625;
    inputs.emissive.present = true;
    material.hasEmissiveFactor = true;
    material.emissiveFactor = {0.125, 0.25, 0.5};
    material.textureInfoFields |= BL_ASSET_TEXTURE_OCCLUSION | BL_ASSET_TEXTURE_EMISSIVE;
    require(l_validateAsset(&data.document, &reason, &data) == BL_OK,
            "normal occlusion emissive roles and independent factors accepted");
    inputs.normal.index = 1;
    require(l_validateAsset(&data.document, &reason, &data) == BL_INVALID_ARGUMENT,
            "invalid normal texture reference before decode");
    inputs.normal.index = 0;
    inputs.normal.scale = NAN;
    require(l_validateAsset(&data.document, &reason, &data) == BL_INVALID_ARGUMENT,
            "nonfinite normal scale before decode");
    inputs.normal.scale = 0;
    require(l_validateAsset(&data.document, &reason, &data) == BL_OK,
            "zero normal scale is not a missing default");
    inputs.occlusion.strength = 1.01;
    require(l_validateAsset(&data.document, &reason, &data) == BL_INVALID_ARGUMENT,
            "occlusion strength range before decode");
    inputs.occlusion.strength = 0;
    require(l_validateAsset(&data.document, &reason, &data) == BL_OK,
            "zero occlusion strength is not a missing default");
    inputs.emissive.texCoord = {true, 2};
    require(l_validateAsset(&data.document, &reason, &data) == BL_UNSUPPORTED,
            "unsupported emissive UV channel not silently zero");
    inputs.emissive.texCoord = {true, 1};
    inputs.emissive.unsupportedFeatures = BL_ASSET_UNSUPPORTED_EXTENSION;
    require(l_validateAsset(&data.document, &reason, &data) == BL_UNSUPPORTED,
            "emissive texture extension still explicitly unsupported");
    inputs.emissive.unsupportedFeatures = 0;
    for (auto status : {BL_OK, BL_CANCELLED, BL_HOST_ERROR})
    {
        Allocations allocations = {};
        Provider provider = {};
        provider.version3 = true;
        provider.status = status;
        provider.buffer = buffer;
        bl_Runtime* r = runtime(&allocations, &provider);
        L_Engine* e = engine(r);
        provider.engine = {r, e->record.id};
        bl_AssetSource source = {BL_ASSET_SOURCE_BYTES, {}, {bytes, sizeof(bytes)}};
        bl_AssetContainer out = {r, UINT64_MAX};
        require(bl_loadGltf(provider.engine, &source, &out) == status && provider.releases == 1,
                "service3 success/partial/cancel release exactly once");
        provider.status = BL_OK; provider.badPrefix = true;
        out = {r, UINT64_MAX};
        require(bl_loadGltf(provider.engine, &source, &out) == BL_INVALID_ARGUMENT &&
                    provider.releases == 2 && out._id == UINT64_MAX,
                "malformed service3 prefix releases safely before trailing reads");
        require(bl_disposeRuntime(r) == BL_OK && !allocations.live, "service3 allocator teardown");
    }
}

static void materialSourcesAndFaults()
{
    unsigned failurePositions = 0;
    for (size_t fault = 0; fault < 128; ++fault)
    {
        Allocations allocations = {};
        bl_Runtime* r = runtime(&allocations);
        if (fault)
        {
            allocations.failAt = allocations.calls + fault;
        }
        L_Material material = {};
        material.testing = true;
        material.pbrExtra = true;
        material.pbrNormalTexture = {r, 1};
        material.pbrOcclusionTexture = {r, 2};
        material.pbrEmissiveTexture = {r, 3};
        material.pbrNormalUv1 = true;
        material.pbrOcclusionUv1 = true;
        material.pbrEmissiveUv1 = true;
        L_Text vertex = {};
        L_Text fragment = {};
        bl_Status status = l_pbrSources(r, true, &vertex, &fragment, true, true, true, true,
                                        &material, true);
        if (status == BL_OK)
        {
            require(strstr(vertex.data, "@location(8)tangent:vec4f") &&
                        strstr(vertex.data, "cross(normalize(normal),T_local)*tangent.w") &&
                        strstr(fragment.data, "normalUv=input.uv2;") &&
                        strstr(fragment.data, "occlusionSampler_,input.uv2") &&
                        strstr(fragment.data, "emissiveSampler,input.uv2") &&
                        strstr(fragment.data, "surfaceFactors:vec4f") &&
                        strstr(fragment.data, "alpha*material.materialAlpha<material.alphaCutoff") &&
                        strstr(fragment.data, "horizonTemp*horizonTemp") &&
                        strstr(fragment.data, "if(front!=mirrored){N=-N;}"),
                    "source-derived all-role instanced tangent/UV/factor/horizon interface");
            require(strstr(fragment.data, "discard;") >
                        strstr(fragment.data, "textureSample(brdfLUT"),
                    "mask discard follows all implicit derivatives without validation suppression");
        }
        else
        {
            require(status == BL_OUT_OF_MEMORY, "source allocation failure status");
            ++failurePositions;
        }
        l_free(r, vertex.data);
        l_free(r, fragment.data);
        allocations.failAt = 0;
        require(bl_disposeRuntime(r) == BL_OK && !allocations.live,
                "partial material source buffers released on every allocation failure");
        if (fault && status == BL_OK)
        {
            break;
        }
    }
    require(failurePositions > 10, "all-role source allocation positions actually exercised");
    printf("Material source allocator failures: %u\n", failurePositions);
}

int main()
{
    hierarchy();
    accessors();
    partialAdd();
    releasesAndFaults();
    textureProtocol3();
    materialSourcesAndFaults();
    puts("Asset raw/schema/deep-hierarchy/locked-matrix/partial-add/reentry/releases/fault tests passed");
}
