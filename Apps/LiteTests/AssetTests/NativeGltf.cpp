#define LITE_TEST_NATIVE_GLTF 1
#include "GpuGeometry.cpp"
#include <thread>

static bl_Status externalFailure = BL_OK;

static bl_Status externalRead(void* user, bl_String uri, bl_HostBuffer* out)
{
    auto* context = static_cast<Context*>(user);
    ++context->reads;
    require(bl_setGltfImportMode(context->runtime, BL_GLTF_IMPORT_SERVICE) == BL_BUSY,
            "native IO callback cannot change routing");
    bl_AssetContainer sentinel = {context->runtime, UINT64_MAX};
    uint8_t byte = 0;
    bl_AssetSource nested = {BL_ASSET_SOURCE_BYTES, {}, {&byte, 1}};
    require(bl_loadGltf(context->engine, &nested, &sentinel) == BL_BUSY &&
                sentinel._id == UINT64_MAX, "native IO callback cannot reenter loading");
    std::filesystem::path path(std::string(uri.data, uri.length));
    if (!path.is_absolute())
    {
        path = context->output / path;
    }
    std::string filePath = path.string();
    auto status = context->io.readFile(context->io.userData,
                                      {filePath.data(), filePath.size()}, out);
    return status == BL_OK ? externalFailure : status;
}

static void sparseFinalIndices(const std::filesystem::path& fixtures,
                               const Json& manifest, bgfx::FrameBufferHandle framebuffer)
{
    const std::string name = "sparse-base-u32";
    auto bytes = file(fixtures / (name + ".glb"));
    uint32_t jsonSize = 0;
    memcpy(&jsonSize, bytes.data() + 12, sizeof(jsonSize));
    auto document = Json::parse(bytes.begin() + 20, bytes.begin() + 20 + jsonSize);
    auto& accessor = document["accessors"][
        document["meshes"][0]["primitives"][0]["indices"].get<size_t>()];
    auto& sparse = accessor["sparse"];
    auto& sparseView = document["bufferViews"][sparse["indices"]["bufferView"].get<size_t>()];
    size_t bin = 20 + jsonSize + 8;
    size_t sparseOffset = bin + sparseView.value("byteOffset", size_t(0)) +
                          sparse["indices"].value("byteOffset", size_t(0));
    uint32_t element = 0;
    memcpy(&element, bytes.data() + sparseOffset, sizeof(element));
    auto& view = document["bufferViews"][accessor["bufferView"].get<size_t>()];
    size_t width = accessor["componentType"] == 5125 ? 4
                   : accessor["componentType"] == 5123 ? 2 : 1;
    size_t offset = bin + view.value("byteOffset", size_t(0)) +
                    accessor.value("byteOffset", size_t(0)) + element * width;
    memset(bytes.data() + offset, 0xff, width);
    Context context;
    setup(context, framebuffer);
    bl_AssetContainer asset = {};
    require(load(context, bytes, &asset) == BL_OK,
            "sparse replacement precedes final index range validation");
    bool checked = false;
    for (const auto& source : manifest["cases"])
    {
        if (source["name"] == name)
        {
            geometry(context, asset, source);
            checked = true;
        }
    }
    require(checked, "independent final sparse index oracle was exercised");
    finish(context);
}

static std::vector<uint8_t> reservedIndexGlb(uint8_t base, bool sparse, uint8_t replacement,
                                          unsigned mode)
{
    std::vector<float> positions(256 * 3, 0);
    positions[0] = -1;
    positions[1] = -1;
    positions[3] = 1;
    positions[4] = -1;
    positions[254 * 3 + 1] = 1;
    positions[255 * 3] = .25f;
    positions[255 * 3 + 1] = .5f;
    std::vector<uint8_t> binary(positions.size() * sizeof(float) + 12, 0);
    memcpy(binary.data(), positions.data(), positions.size() * sizeof(float));
    size_t offset = positions.size() * sizeof(float);
    binary[offset] = 0;
    binary[offset + 1] = 1;
    binary[offset + 2] = base;
    binary[offset + 4] = 2;
    binary[offset + 8] = replacement;
    Json document = {
        {"asset", {{"version", "2.0"}}},
        {"buffers", {{{"byteLength", binary.size()}}}},
        {"bufferViews", {
            {{"buffer", 0}, {"byteOffset", 0}, {"byteLength", offset}, {"target", 34962}},
            {{"buffer", 0}, {"byteOffset", offset}, {"byteLength", 3}, {"target", 34963}},
            {{"buffer", 0}, {"byteOffset", offset + 4}, {"byteLength", 1}},
            {{"buffer", 0}, {"byteOffset", offset + 8}, {"byteLength", 1}}}},
        {"accessors", {
            {{"bufferView", 0}, {"componentType", 5126}, {"count", 256}, {"type", "VEC3"},
             {"min", {-1, -1, 0}}, {"max", {1, 1, 0}}},
            {{"bufferView", 1}, {"componentType", 5121}, {"count", 3}, {"type", "SCALAR"}}}},
        {"meshes", {{{"primitives", {{{"attributes", {{"POSITION", 0}}},
                                     {"indices", 1}, {"mode", mode}}}}}}},
        {"nodes", {{{"mesh", 0}}}},
        {"scenes", {{{"nodes", {0}}}}},
        {"scene", 0}
    };
    if (sparse)
    {
        document["accessors"][1]["sparse"] = {
            {"count", 1}, {"indices", {{"bufferView", 2}, {"componentType", 5121}}},
            {"values", {{"bufferView", 3}}}};
    }
    std::string text = document.dump();
    while (text.size() % 4)
    {
        text.push_back(' ');
    }
    uint32_t header[5] = {0x46546c67, 2, (uint32_t)(28 + text.size() + binary.size()),
                          (uint32_t)text.size(), 0x4e4f534a};
    uint32_t binHeader[2] = {(uint32_t)binary.size(), 0x004e4942};
    std::vector<uint8_t> bytes(header[2]);
    memcpy(bytes.data(), header, sizeof(header));
    memcpy(bytes.data() + 20, text.data(), text.size());
    memcpy(bytes.data() + 20 + text.size(), binHeader, sizeof(binHeader));
    memcpy(bytes.data() + 28 + text.size(), binary.data(), binary.size());
    return bytes;
}

static Json reservedIndices(const std::filesystem::path& output,
                            bgfx::FrameBufferHandle framebuffer)
{
    struct Case
    {
        const char* name;
        uint8_t base;
        bool sparse;
        uint8_t replacement;
        bl_Status expected;
    };
    const Case cases[] = {
        {"nearby-254", 254, false, 0, BL_OK},
        {"reserved-255", 255, false, 0, BL_INVALID_ARGUMENT},
        {"sparse-replaces-reserved-base", 255, true, 254, BL_OK},
        {"sparse-introduces-reserved", 254, true, 255, BL_INVALID_ARGUMENT}
    };
    auto directory = std::filesystem::absolute(output) / "reserved-indices";
    std::filesystem::create_directories(directory);
    Json results = Json::array();
    for (unsigned mode : {4u, 5u, 6u})
    {
        for (const auto& source : cases)
        {
            auto bytes = reservedIndexGlb(source.base, source.sparse, source.replacement, mode);
            auto path = directory / (std::string(source.name) + "-" + std::to_string(mode) + ".glb");
            std::ofstream(path, std::ios::binary).write((const char*)bytes.data(),
                                                       (std::streamsize)bytes.size());
            for (bool uri : {false, true})
            {
                Context context;
                setup(context, framebuffer);
                bl_AssetContainer asset = {context.runtime, UINT64_MAX};
                std::string filename = path.string();
                bl_AssetSource input = uri
                    ? bl_AssetSource{BL_ASSET_SOURCE_URI, {filename.data(), filename.size()}, {}}
                    : bl_AssetSource{BL_ASSET_SOURCE_BYTES, {}, {bytes.data(), bytes.size()}};
                bl_Status status = bl_loadGltf(context.engine, &input, &asset);
                if (status != source.expected)
                {
                    fprintf(stderr, "Reserved-index case %s mode%u uri%d: status%d expected%d\n",
                            source.name, mode, int(uri), int(status), int(source.expected));
                }
                require(status == source.expected, "final resolved U8 index reserves 255 with 256 vertices");
                if (status == BL_OK)
                {
                    auto* owner = (L_Asset*)l_peek(context.runtime, asset._id);
                    size_t meshes = 0;
                    for (size_t i = 0; i < owner->nodeCount; ++i)
                    {
                        auto* record = l_peek(context.runtime, owner->nodes[i]._id);
                        if (record->kind == L_MESH)
                        {
                            auto* mesh = (L_Mesh*)record;
                            const auto& geometry = mesh->geometry;
                            require(geometry.vertices == 256 && geometry.indexCount == 3 &&
                                        geometry.indices[mode == 6 ? 1 : 2] == 254 &&
                                        geometry.streams[0][254 * 3 + 1] == 1 &&
                                        bgfx::isValid(geometry.vertexBuffer) &&
                                        bgfx::isValid(geometry.indexBuffer),
                                    "accepted boundary and sparse cases create exact registered GPU geometry");
                            ++meshes;
                        }
                    }
                    require(meshes == 1, "one real primitive, not a dropped or substituted mesh");
                }
                else
                {
                    require(asset._runtime == context.runtime && asset._id == UINT64_MAX,
                            "reserved final index preserves complete output handle");
                }
                require(context.parses == 0, "reserved-index regression uses the actual native importer");
                finish(context);
                results.push_back({{"case", source.name}, {"mode", mode},
                                   {"sourceKind", uri ? "URI" : "BYTES"}, {"status", status},
                                   {"trackedTeardownBytes", 0}});
            }
        }
    }
    require(results.size() == 24, "all independent reserved-index route/topology controls executed");
    return results;
}

static Json directExternal(const std::filesystem::path& fixture, const Json& oracle,
                           const std::filesystem::path& output,
                           bgfx::FrameBufferHandle framebuffer)
{
    auto glb = file(fixture);
    uint32_t jsonSize = 0;
    memcpy(&jsonSize, glb.data() + 12, sizeof(jsonSize));
    auto document = Json::parse(glb.begin() + 20, glb.begin() + 20 + jsonSize);
    auto* bin = glb.data() + 20 + jsonSize + 8;
    auto directory = std::filesystem::absolute(output) / "external";
    std::filesystem::create_directories(directory);
    document["buffers"][0]["uri"] = "buffer%20data.bin";
    size_t binSize = document["buffers"][0]["byteLength"];
    std::ofstream binary(directory / "buffer data.bin", std::ios::binary);
    binary.write((const char*)bin, (std::streamsize)binSize);
    binary.close();
    for (size_t i = 0; i < document["images"].size(); ++i)
    {
        auto& image = document["images"][i];
        const auto& view = document["bufferViews"][image["bufferView"].get<size_t>()];
        std::string name = "image-" + std::to_string(i) + ".png";
        size_t offset = view.value("byteOffset", size_t(0));
        size_t bytes = view["byteLength"];
        std::ofstream encoded(directory / name, std::ios::binary);
        encoded.write((const char*)bin + offset, (std::streamsize)bytes);
        image.erase("bufferView");
        image["uri"] = name;
    }
    std::string text = document.dump();
    std::ofstream(directory / "model.gltf") << text;
    for (bool memory : {false, true})
    {
        Context context;
        context.output = directory;
        setup(context, framebuffer);
        context.runtime->io.readFile = externalRead;
        bl_AssetContainer asset = {};
        std::string path = (directory / "model.gltf").string();
        bl_AssetSource source = memory
            ? bl_AssetSource{BL_ASSET_SOURCE_BYTES, {}, {(const uint8_t*)text.data(), text.size()}}
            : bl_AssetSource{BL_ASSET_SOURCE_URI, {path.data(), path.size()}, {}};
        require(bl_loadGltf(context.engine, &source, &asset) == BL_OK,
                "actual JSON URI/BYTES with percent-escaped external buffer and PNG images");
        geometry(context, asset, oracle);
        require(context.parses == 0 && context.images > 0 && context.reads >= 2,
                "external native resources use IO/image services, never raw schema translation");
        finish(context);
    }
    for (bl_Status expected : {BL_CANCELLED, BL_HOST_ERROR, BL_OUT_OF_MEMORY})
    {
        Context context;
        context.output = directory;
        setup(context, framebuffer);
        context.runtime->io.readFile = externalRead;
        externalFailure = expected;
        std::vector<uint8_t> bytes(text.begin(), text.end());
        bl_AssetContainer sentinel = {context.runtime, UINT64_MAX};
        require(load(context, bytes, &sentinel) == expected && sentinel._id == UINT64_MAX,
                "partial external buffer errors preserve exact callback status");
        finish(context);
        externalFailure = BL_OK;
    }
    document["buffers"][0]["uri"] = "missing.bin";
    text = document.dump();
    Context context;
    context.output = directory;
    setup(context, framebuffer);
    context.runtime->io.readFile = externalRead;
    std::vector<uint8_t> missing(text.begin(), text.end());
    bl_AssetContainer sentinel = {context.runtime, UINT64_MAX};
    require(load(context, missing, &sentinel) != BL_OK && sentinel._id == UINT64_MAX,
            "missing external data is not a successful empty mesh");
    finish(context);
    return {{"jsonUriAndCallerBytes", true}, {"externalBuffersAndImages", true},
            {"percentEscapedUri", true}, {"callbackReentryRejected", true},
            {"partialIoFailures", 3}, {"trackedTeardownBytes", 0}};
}

static Json directFaults(const std::filesystem::path& fixture,
                         bgfx::FrameBufferHandle framebuffer)
{
    auto bytes = file(fixture);
    size_t faults = 0;
    bool completed = false;
    for (size_t fault = 1; fault < 2048; ++fault)
    {
        fprintf(stderr, "Native allocation fault %zu\n", fault);
        Context context;
        setup(context, framebuffer);
        context.memory.failAt = context.memory.calls + fault;
        bl_AssetContainer sentinel = {context.runtime, UINT64_MAX};
        bl_Status status = load(context, bytes, &sentinel);
        require(status == BL_OK || status == BL_OUT_OF_MEMORY,
                "native importer failpoint returns OOM, never partial success");
        if (status != BL_OK)
        {
            require(sentinel._id == UINT64_MAX, "native OOM output is unchanged");
            ++faults;
        }
        require(context.parses == 0 && context.parsedReleases == 0,
                "native importer never invokes raw provider");
        finish(context);
        if (status == BL_OK)
        {
            completed = true;
            break;
        }
    }
    require(completed, "complete native allocator failpoint sweep reached success");
    for (bool empty : {false, true})
    {
        for (bl_Status expected : {BL_CANCELLED, BL_HOST_ERROR, BL_OUT_OF_MEMORY})
        {
            Context context;
            setup(context, framebuffer);
            context.imageFailure = expected;
            context.imageEmpty = empty;
            bl_AssetContainer sentinel = {context.runtime, UINT64_MAX};
            require(load(context, bytes, &sentinel) == expected &&
                        sentinel._id == UINT64_MAX,
                    "native partial and empty image errors preserve exact status and output");
            finish(context);
        }
    }
    Context context;
    setup(context, framebuffer);
    require(bl_setGltfImportMode(nullptr, BL_GLTF_IMPORT_NATIVE) == BL_INVALID_ARGUMENT,
            "mode rejects null runtime");
    require(bl_setGltfImportMode(context.runtime, (bl_GltfImportMode)19) == BL_INVALID_ARGUMENT &&
                context.runtime->gltfImportMode == BL_GLTF_IMPORT_NATIVE,
            "invalid mode preserves state");
    bl_Status wrongThread = BL_OK;
    std::thread thread([&]() {
        wrongThread = bl_setGltfImportMode(context.runtime, BL_GLTF_IMPORT_SERVICE);
    });
    thread.join();
    require(wrongThread == BL_WRONG_THREAD &&
                context.runtime->gltfImportMode == BL_GLTF_IMPORT_NATIVE,
            "mode thread affinity preserves state");
    l_enterDispatch(context.runtime);
    require(bl_setGltfImportMode(context.runtime, BL_GLTF_IMPORT_SERVICE) == BL_BUSY,
            "mode rejects dispatch mutation");
    l_leaveDispatch(context.runtime);
    l_enterHost(context.runtime);
    require(bl_setGltfImportMode(context.runtime, BL_GLTF_IMPORT_SERVICE) == BL_BUSY,
            "mode rejects host callback reentry");
    l_leaveHost(context.runtime);
    bl_AssetContainer asset = {};
    require(load(context, bytes, &asset) == BL_OK && context.parses == 0,
            "native load with independent real handles");
    auto* owner = (L_Asset*)l_peek(context.runtime, asset._id);
    bl_SceneNode root = owner->root;
    require(bl_setGltfImportMode(context.runtime, BL_GLTF_IMPORT_SERVICE) == BL_OK,
            "legacy route restored for future loads");
    bl_AssetContainer legacy = {};
    require(load(context, bytes, &legacy) == BL_OK && context.parses == 1 &&
                owner->root._id == root._id,
            "future legacy load leaves live native asset unchanged");
    require(bl_disposeAssetContainer(legacy) == BL_OK &&
                bl_disposeAssetContainer(asset) == BL_OK, "independent route ownership");
    require(bl_setGltfImportMode(context.runtime, BL_GLTF_IMPORT_NATIVE) == BL_OK,
            "restore native route");
    bl_SceneContext scenes[2] = {};
    require(load(context, bytes, &asset) == BL_OK &&
                bl_createSceneContext(context.engine, &scenes[0]) == BL_OK &&
                bl_createSceneContext(context.engine, &scenes[1]) == BL_OK &&
                bl_addAssetContainerToScene(scenes[0], asset) == BL_OK &&
                bl_addAssetContainerToScene(scenes[1], asset) == BL_OK,
            "native asset has independent shared-scene owners");
    require(bl_removeAssetContainerFromScene(scenes[0], asset) == BL_OK &&
                bl_disposeAssetContainer(asset) == BL_BUSY,
            "one scene cannot retire another scene's native resources");
    require(bl_removeAssetContainerFromScene(scenes[1], asset) == BL_OK &&
                bl_disposeAssetContainer(asset) == BL_OK,
            "last native owner retires resources");
    context.runtime->hasAssets = false;
    std::string numeric = R"({"asset":{"version":"2.0"},"nodes":[{"translation":[16777217,0,0]}],"scenes":[{"nodes":[0]}]})";
    std::vector<uint8_t> numericBytes(numeric.begin(), numeric.end());
    require(load(context, numericBytes, &asset) == BL_OK,
            "native JSON operates without a raw glTF parser service");
    owner = (L_Asset*)l_peek(context.runtime, asset._id);
    bl_NodeChildren children = {};
    bl_Vec3 position = {};
    require(bl_getNodeChildren(owner->root, &children) == BL_OK && children.count == 1 &&
                bl_getNodePosition(children.data[0], &position) == BL_OK &&
                position.x == 16777216.0,
            "opt-in native Float32 JSON adaptation is explicit, not a vendor type hack");
    require(bl_disposeAssetContainer(asset) == BL_OK, "numeric JSON lifecycle");
    context.runtime->hasAssets = true;
    size_t warmBytes = 0;
    size_t warmSlots = 0;
    for (size_t i = 0; i < 40; ++i)
    {
        require(load(context, bytes, &asset) == BL_OK &&
                    bl_disposeAssetContainer(asset) == BL_OK, "native identity churn");
        if (i == 5)
        {
            warmBytes = context.memory.bytes;
            warmSlots = context.runtime->count;
        }
        if (i > 5)
        {
            require(warmBytes == context.memory.bytes && warmSlots == context.runtime->count,
                    "native allocations and slots plateau");
        }
    }
    const std::vector<std::string> invalid = {
        "{", R"({"asset":{"version":"1.0"}})",
        R"({"asset":{"version":"2.0"},"extensionsRequired":["UNKNOWN"]})",
        R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":4096,"uri":"data:application/octet-stream;base64,AA=="}]})",
        R"({"asset":{"version":"2.0"},"nodes":[{"children":[0]}],"scenes":[{"nodes":[0]}]})",
        R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":4}],"bufferViews":[{"buffer":0,"byteOffset":18446744073709551612,"byteLength":8}]})"};
    for (const auto& input : invalid)
    {
        std::vector<uint8_t> data(input.begin(), input.end());
        bl_AssetContainer sentinel = {context.runtime, UINT64_MAX};
        require(load(context, data, &sentinel) != BL_OK && sentinel._id == UINT64_MAX,
                "native malformed/required-extension/IO/cycle controls reject without publishing");
    }
    finish(context);
    return {{"allocatorFaultPositions", faults}, {"partialEmptyImageFaults", 6},
            {"modeRuntimeThreadDispatchHostControls", true}, {"legacyRoutingPreserved", true},
            {"nativeNeedsNoRawParserService", true}, {"nativeFloat32JsonInput", true},
            {"sharedSceneOwnerRetirement", true},
            {"nativeChurnIterations", 40}, {"trackedTeardownBytes", 0}};
}

int main(int argc, char** argv)
{
    require(argc == 4 || argc == 5, "native fixtures, lighting, output, optional faults-only/indices-only");
    bool faultsOnly = argc == 5 && std::string(argv[4]) == "faults-only";
    bool indicesOnly = argc == 5 && std::string(argv[4]) == "indices-only";
    std::filesystem::path fixtures(argv[1]);
    std::filesystem::path lighting(argv[2]);
    std::filesystem::path output(argv[3]);
    std::filesystem::create_directories(output);
    auto manifest = Json::parse(std::ifstream(fixtures / "geometry-cases.json"));
    require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "image COM");
    HWND window = CreateWindowExW(0, L"STATIC", L"Native glTF GPU tests", WS_POPUP, 0, 0, 64, 64,
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
    size_t rendered = 0;
    size_t rejected = 0;
    std::filesystem::path faultFixture;
    Json faultOracle;
    for (const auto& source : manifest["cases"])
    {
        std::string name = source["name"].get<std::string>();
        auto fixture = fixtures / (name + ".glb");
        if (source["expected"] == "BL_OK")
        {
            if (faultFixture.empty())
            {
                faultFixture = fixture;
                faultOracle = source;
            }
            if (faultsOnly || indicesOnly)
            {
                continue;
            }
            render(fixture, lighting, output, source, framebuffer, attachments[0], "");
            ++rendered;
        }
        else
        {
            if (faultsOnly || indicesOnly)
            {
                continue;
            }
            Context context;
            setup(context, framebuffer);
            auto bytes = file(fixture);
            bl_AssetContainer sentinel = {context.runtime, UINT64_MAX};
            require(load(context, bytes, &sentinel) != BL_OK && sentinel._id == UINT64_MAX,
                    "native geometry negative controls reject actual source bytes");
            finish(context);
            ++rejected;
        }
    }
    Json faults = Json::object();
    Json external = Json::object();
    Json reserved = Json::array();
    if (!manifest.value("cameraIgnorePolicy", false))
    {
        reserved = reservedIndices(output, framebuffer);
    }
    if (!manifest.value("cameraIgnorePolicy", false) && !indicesOnly)
    {
        faults = directFaults(faultFixture, framebuffer);
        sparseFinalIndices(fixtures, manifest, framebuffer);
        external = directExternal(faultFixture, faultOracle, output, framebuffer);
    }
    bgfx::destroy(framebuffer);
    bgfx::frame();
    bgfx::shutdown();
    DestroyWindow(window);
    CoUninitialize();
    Json result = {{"nativeRenderedCases", rendered}, {"nativeRejectedCases", rejected},
                   {"actualReadback", true}, {"assertions", assertions},
                   {"retirements", retirements}, {"faults", faults}, {"external", external},
                   {"reservedIndices", reserved}};
    std::ofstream(output / "result.json") << result.dump(2) << '\n';
    printf("Native glTF: %zu rendered, %zu rejected, %zu assertions\n", rendered, rejected, assertions);
    return 0;
}
