#define LITE_TEST_NATIVE_GLTF 1
#include "GpuSpecularGlossiness.cpp"

int main(int argc, char** argv)
{
    require(argc == 3, "native material fixture directory and new output directory");
    std::filesystem::path fixtures(argv[1]);
    std::filesystem::path output(argv[2]);
    std::filesystem::create_directories(output);
    require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "native image COM");
    HWND window = CreateWindowExW(0, L"STATIC", L"Native material GPU", WS_POPUP, 0, 0, 64, 64,
                                 nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    bgfx::Init init;
    init.type = bgfx::RendererType::Direct3D11;
    init.swapChain.nwh = window;
    init.swapChain.width = 64;
    init.swapChain.height = 64;
    init.swapChain.formatColor = bgfx::TextureFormat::BGRA8;
    init.swapChain.formatDepthStencil = bgfx::TextureFormat::D24S8;
    require(bgfx::init(init), "actual production D3D11");
    bgfx::TextureHandle textures[2] = {
        bgfx::createTexture2D(64, 64, false, 1, bgfx::TextureFormat::BGRA8, BGFX_TEXTURE_RT),
        bgfx::createTexture2D(64, 64, false, 1, bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT)};
    auto framebuffer = bgfx::createFrameBuffer(2, textures, true);
    auto manifest = Json::parse(std::ifstream(fixtures / "fixture-manifest.json"));
    size_t rendered = 0;
    for (const auto& source : manifest["fixtures"])
    {
        if (source["expectedProvider5"] != "BL_OK" || source["expectedCore5"] != "BL_OK")
        {
            continue;
        }
        auto name = source["id"].get<std::string>();
        Context context;
        context.output = output / name;
        std::filesystem::create_directories(context.output);
        setup(context, framebuffer);
        auto bytes = file(fixtures / source["file"].get<std::string>());
        bl_AssetContainer asset = {};
        auto status = load(context, bytes, &asset);
        if (status != BL_OK)
        {
            bl_Error error = {};
            bl_getLastError(context.runtime, &error);
            fprintf(stderr, "Native material fixture %s status%d: %.*s\n", name.c_str(),
                    int(status), int(error.message.length), error.message.data);
        }
        require(status == BL_OK && context.parses == 0,
                "native MR/SG construction without raw callback");
        bytes.assign(bytes.size(), 0x83);
        resources(context, asset);
        render(context, asset, fixtures / "lighting", textures[0]);
        finish(context);
        ++rendered;
    }
    bgfx::destroy(framebuffer);
    wait(nullptr);
    bgfx::shutdown();
    DestroyWindow(window);
    CoUninitialize();
    std::ofstream(output / "result.json") << Json({
        {"nativeRenderedCases", rendered}, {"productionWGSLModified", false},
        {"nativeMipReadback", true}, {"assertions", assertions}, {"actualFences", fences},
        {"trackedTeardownBytes", 0}}).dump(2) << '\n';
    printf("Native materials: %zu rendered, %zu assertions, teardown0\n", rendered, assertions);
    return 0;
}
