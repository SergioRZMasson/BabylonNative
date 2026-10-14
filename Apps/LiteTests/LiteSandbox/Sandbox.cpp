#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj_core.h>
#include <objbase.h>
#include <bgfx/bgfx.h>
#include <babylon_lite.h>
#include <babylon_lite_asset_decoder.h>
#include <babylon_lite_shader_compiler.h>
#include "AssetIO.h"
#include "SceneCamera.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <malloc.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

static constexpr uint32_t Width = 1280;
static constexpr uint32_t Height = 720;

struct Memory
{
    size_t live = 0;
    size_t bytes = 0;
};

static void* allocate(void* user, size_t bytes, size_t alignment)
{
    auto* memory = static_cast<Memory*>(user);
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

static std::string utf8(const std::wstring& text)
{
    if (text.empty())
    {
        return {};
    }
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                                    static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!count)
    {
        throw std::runtime_error("Invalid Unicode path");
    }
    std::string result(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                        result.data(), count, nullptr, nullptr);
    return result;
}

static std::wstring wide(const std::string& text)
{
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                    static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(count), L'\0');
    if (count)
    {
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                            static_cast<int>(text.size()), result.data(), count);
    }
    return result;
}

static bl_String span(const std::string& text)
{
    return {text.data(), text.size()};
}

static void checked(bl_Status status, const char* operation)
{
    if (status != BL_OK)
    {
        throw std::runtime_error(std::string(operation) + " returned " + std::to_string(status));
    }
}

struct Capture : bgfx::CallbackI
{
    std::atomic<bool> done = false;
    bool failed = false;

    void fatal(const char*, uint16_t, bgfx::Fatal::Enum, const char* message) override
    {
        std::fprintf(stderr, "bgfx fatal: %s\n", message);
        std::exit(3);
    }
    void traceVargs(const char*, uint16_t, const char*, va_list) override {}
    void profilerBegin(const char*, uint32_t, const char*, uint16_t) override {}
    void profilerBeginLiteral(const char*, uint32_t, const char*, uint16_t) override {}
    void profilerEnd() override {}
    uint32_t cacheReadSize(uint64_t) override { return 0; }
    bool cacheRead(uint64_t, void*, uint32_t) override { return false; }
    void cacheWrite(uint64_t, const void*, uint32_t) override {}
    void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool) override {}
    void captureEnd() override {}
    void captureFrame(const void*, uint32_t) override {}
    void screenShot(const char* path, uint32_t width, uint32_t height, uint32_t pitch,
                     bgfx::TextureFormat::Enum format, const void* bytes, uint32_t, bool flip) override
    {
        try
        {
            std::ofstream file(std::filesystem::path(wide(path)), std::ios::binary);
            failed = width != Width || height != Height || format != bgfx::TextureFormat::BGRA8;
            for (uint32_t y = 0; y < height && !failed; ++y)
            {
                uint32_t row = flip ? height - 1 - y : y;
                file.write(static_cast<const char*>(bytes) + row * pitch,
                            static_cast<std::streamsize>(width * 4));
            }
            failed |= !file.good();
        }
        catch (...)
        {
            failed = true;
        }
        done = true;
    }
};

static bool initialized(void*)
{
    return true;
}

static bl_Status waitSubmitted(void*)
{
    const uint8_t marker[4] = {29, 53, 79, 255};
    uint8_t data[4] = {};
    auto source = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0,
                                       bgfx::copy(marker, 4));
    auto target = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8,
                                       BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST);
    if (!bgfx::isValid(source) || !bgfx::isValid(target))
    {
        if (bgfx::isValid(source))
        {
            bgfx::destroy(source);
        }
        if (bgfx::isValid(target))
        {
            bgfx::destroy(target);
        }
        return BL_OUT_OF_MEMORY;
    }
    bgfx::TextureRegion from;
    from.init(source);
    bgfx::TextureRegion to;
    to.init(target);
    bgfx::blit(126, to, from);
    uint32_t ready = bgfx::read(to, data);
    uint32_t current = bgfx::frame();
    while (static_cast<int32_t>(current - ready) < 0)
    {
        current = bgfx::frame();
    }
    bgfx::destroy(source);
    bgfx::destroy(target);
    return memcmp(data, marker, 4) == 0 ? BL_OK : BL_HOST_ERROR;
}

struct Model
{
    bl_SceneContext scene = {};
    bl_AssetContainer asset = {};
    bl_EnvironmentTextures environment = {};
    bl_ArcRotateCamera camera = {};
    bl_ArcRotateControl control = {};
    std::wstring path;
    size_t sourceMeshes = 0;
    size_t sourcePrimitives = 0;
    size_t sourceVertices = 0;
    size_t sourceMaterials = 0;
    size_t selectedPrimitives = 0;
    size_t selectedNodes = 0;
    uint32_t selectedScene = 0;
    bool emptyCamera = false;
};

struct Host
{
    HWND window = nullptr;
    bl_Runtime* runtime = nullptr;
    bl_EngineContext engine = {};
    bl_AssetDecoder* decoder = nullptr;
    Memory memory;
    Capture capture;
    Model model;
    std::filesystem::path assets;
    std::deque<std::wstring> pending;
    std::string message;
    bool quit = false;
    bool loaded = false;
    bool memorySource = false;
    bl_AssetService decoderService = {};
    Model parsedCounts;
    bl_Status loadStatus = BL_OK;
    size_t successfulDrops = 0;
    size_t failedDrops = 0;
};

static bl_Status parseForHost(void* user, bl_Bytes bytes, bl_String base, bl_ParsedGltf* out)
{
    auto* host = static_cast<Host*>(user);
    bl_Status status = host->decoderService.parseGltf(host->decoderService.userData, bytes, base, out);
    if (status != BL_OK)
    {
        return status;
    }
    try
    {
        host->parsedCounts.sourceMeshes = out->meshCount;
        host->parsedCounts.sourceMaterials = out->materialCount;
        host->parsedCounts.sourcePrimitives = 0;
        host->parsedCounts.sourceVertices = 0;
        host->parsedCounts.selectedScene = out->defaultScene.present ? out->defaultScene.value : 0;
        host->parsedCounts.selectedPrimitives = 0;
        host->parsedCounts.selectedNodes = 0;
        for (size_t i = 0; i < out->meshCount; ++i)
        {
            const auto& mesh = out->meshes[i];
            host->parsedCounts.sourcePrimitives += mesh.primitiveCount;
            for (size_t j = 0; j < mesh.primitiveCount; ++j)
            {
                const auto& primitive = mesh.primitives[j];
                for (size_t k = 0; k < primitive.attributeCount; ++k)
                {
                    const auto& attribute = primitive.attributes[k];
                    if (attribute.semantic.length == 8 &&
                        !memcmp(attribute.semantic.data, "POSITION", 8) &&
                        attribute.accessor < out->accessorCount)
                    {
                        host->parsedCounts.sourceVertices += out->accessors[attribute.accessor].count;
                    }
                }
            }
        }
        // Core validates all references; host counts only the selected scene for its camera policy.
        std::vector<uint32_t> pending;
        std::vector<bool> seen(out->nodeCount, false);
        if (host->parsedCounts.selectedScene < out->sceneCount)
        {
            const auto& scene = out->scenes[host->parsedCounts.selectedScene];
            for (size_t i = 0; i < scene.rootCount; ++i)
            {
                pending.push_back(scene.roots[i]);
            }
        }
        while (!pending.empty())
        {
            uint32_t index = pending.back();
            pending.pop_back();
            if (index >= out->nodeCount || seen[index])
            {
                continue;
            }
            seen[index] = true;
            ++host->parsedCounts.selectedNodes;
            const auto& node = out->nodes[index];
            if (node.mesh.present && node.mesh.value < out->meshCount)
            {
                host->parsedCounts.selectedPrimitives += out->meshes[node.mesh.value].primitiveCount;
            }
            for (size_t i = 0; i < node.childCount; ++i)
            {
                pending.push_back(node.children[i]);
            }
        }
    }
    catch (const std::bad_alloc&)
    {
        return BL_OUT_OF_MEMORY;
    }
    catch (...)
    {
        return BL_HOST_ERROR;
    }
    return status;
}

static void releaseParsedForHost(void* user, bl_ParsedGltf* out)
{
    auto* host = static_cast<Host*>(user);
    host->decoderService.releaseGltf(host->decoderService.userData, out);
}

static bl_Status parseEnvironmentForHost(void* user, bl_Bytes bytes, bl_ParsedEnvironment* out)
{
    auto* host = static_cast<Host*>(user);
    return host->decoderService.parseEnvironment(host->decoderService.userData, bytes, out);
}

static void releaseEnvironmentForHost(void* user, bl_ParsedEnvironment* out)
{
    auto* host = static_cast<Host*>(user);
    host->decoderService.releaseEnvironment(host->decoderService.userData, out);
}

static void title(Host& host, const std::string& text)
{
    host.message = text;
    std::wstring caption = L"Babylon Lite native GLB sandbox | " + wide(text);
    SetWindowTextW(host.window, caption.c_str());
    std::printf("%s\n", text.c_str());
    std::fflush(stdout);
}

static std::string lastError(Host& host, bl_Status status)
{
    bl_Error error = {};
    if (bl_getLastError(host.runtime, &error) == BL_OK && error.status == status &&
        error.message.length)
    {
        return std::string(error.message.data, error.message.length);
    }
    return "Native status " + std::to_string(status);
}

static void disposeModel(Host& host, Model& model)
{
    if (model.control._id)
    {
        checked(bl_detachControl(model.control), "Detach control");
    }
    if (model.scene._id)
    {
        checked(bl_unregisterScene(model.scene), "Unregister scene");
        checked(bl_disposeScene(model.scene), "Dispose scene");
    }
    if (model.camera._id)
    {
        checked(bl_disposeNode({host.runtime, model.camera._id}), "Dispose camera");
    }
    if (model.asset._id)
    {
        checked(bl_disposeAssetContainer(model.asset), "Dispose asset");
    }
    if (model.environment._id)
    {
        checked(bl_disposeEnvironmentTextures(model.environment), "Dispose environment");
    }
    model = {};
}

static void loadModel(Host& host, const std::wstring& path)
{
    title(host, "Loading " + utf8(std::filesystem::path(path).filename().wstring()) +
                    " synchronously...");
    Model candidate;
    std::string file = utf8(path);
    std::string env = utf8((host.assets / "environmentSpecular.env").wstring());
    std::string brdf = utf8((host.assets / "brdf-lut.png").wstring());
    bl_AssetSource source = {BL_ASSET_SOURCE_URI, span(file), {}};
    bl_HostBuffer bytes = {};
    bl_Status status = BL_OK;
    std::string hostError;
    auto io = SandboxAssetIO();
    if (host.memorySource)
    {
        status = io.readFile(io.userData, span(file), &bytes);
        if (status != BL_OK)
        {
            hostError = status == BL_OUT_OF_MEMORY
                            ? "Unable to allocate GLB input bytes"
                            : "Unable to read GLB file; check file existence, permissions and size";
        }
        source = {BL_ASSET_SOURCE_BYTES, {}, {bytes.data, bytes.byteCount}};
    }
    if (status == BL_OK)
    {
        status = bl_loadGltf(host.engine, &source, &candidate.asset);
    }
    if (host.memorySource)
    {
        io.releaseBuffer(io.userData, &bytes);
    }
    if (status == BL_OK)
    {
        status = bl_createSceneContext(host.engine, &candidate.scene);
    }
    if (status == BL_OK)
    {
        status = bl_addAssetContainerToScene(candidate.scene, candidate.asset);
    }
    if (status == BL_OK)
    {
        candidate.emptyCamera = host.parsedCounts.selectedPrimitives == 0;
    }
    if (status == BL_OK && !candidate.emptyCamera)
    {
        bl_EnvironmentOptions options = {};
        options.brdfUrl = span(brdf);
        options.skipGround = BL_BOOL_TRUE;
        options.skipSkybox = BL_BOOL_TRUE;
        status = bl_loadEnvironment(candidate.scene, span(env), &options, &candidate.environment);
    }
    if (status == BL_OK)
    {
        status = sandboxSceneCamera(candidate.scene, candidate.emptyCamera, &candidate.camera);
    }
    if (status == BL_OK)
    {
        status = bl_attachControl(candidate.camera, candidate.scene, nullptr, &candidate.control);
    }
    bool switched = false;
    if (status == BL_OK)
    {
        status = bl_registerScene(candidate.scene);
    }
    if (status == BL_OK && host.model.scene._id)
    {
        status = bl_unregisterScene(host.model.scene);
        switched = status == BL_OK;
    }
    if (status == BL_OK)
    {
        status = bl_frame(host.engine, 1000.0 / 60);
        bgfx::frame();
    }
    bl_EngineStats stats = {};
    if (status == BL_OK)
    {
        status = bl_getEngineStats(host.engine, &stats);
        if (status == BL_OK && !candidate.emptyCamera && !stats.drawCallCount)
        {
            status = BL_UNSUPPORTED;
        }
        else if (status == BL_OK && candidate.emptyCamera && stats.drawCallCount)
        {
            status = BL_HOST_ERROR;
        }
    }
    host.loadStatus = status;
    host.loaded = status == BL_OK;
    if (status != BL_OK)
    {
        std::string error = hostError.empty() ? lastError(host, status) : hostError;
        disposeModel(host, candidate);
        if (switched)
        {
            checked(bl_registerScene(host.model.scene), "Restore previous scene");
        }
        ++host.failedDrops;
        title(host, "Load failed [" + std::to_string(status) + "]: " + error +
                        (host.model.asset._id ? " | previous model retained" : " | drag another GLB"));
        return;
    }
    disposeModel(host, host.model);
    candidate.path = path;
    candidate.sourceMeshes = host.parsedCounts.sourceMeshes;
    candidate.sourcePrimitives = host.parsedCounts.sourcePrimitives;
    candidate.sourceVertices = host.parsedCounts.sourceVertices;
    candidate.sourceMaterials = host.parsedCounts.sourceMaterials;
    candidate.selectedPrimitives = host.parsedCounts.selectedPrimitives;
    candidate.selectedNodes = host.parsedCounts.selectedNodes;
    candidate.selectedScene = host.parsedCounts.selectedScene;
    host.model = std::move(candidate);
    ++host.successfulDrops;
    title(host, "Loaded " + utf8(std::filesystem::path(path).filename().wstring()) + " | " +
                    std::to_string(host.model.sourceMeshes) + " source meshes / " +
                    std::to_string(host.model.sourceVertices) + " vertices / " +
                    std::to_string(host.model.sourceMaterials) + " materials / " +
                    std::to_string(host.model.selectedPrimitives) + " selected primitives / " +
                    std::to_string(stats.drawCallCount) +
                    " actual draws | left-drag orbit, wheel zoom | drop another GLB");
}

static void pointer(Host& host, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (!host.model.control._id)
    {
        return;
    }
    bl_ArcRotateInput input = {};
    input.kind = message == WM_LBUTTONDOWN ? BL_ARC_POINTER_DOWN
                 : message == WM_LBUTTONUP ? BL_ARC_POINTER_UP
                 : message == WM_MOUSEWHEEL ? BL_ARC_WHEEL
                                            : BL_ARC_POINTER_MOVE;
    input.pointerType = BL_ARC_POINTER_MOUSE;
    input.pointerId = 1;
    input.clientX = GET_X_LPARAM(lparam);
    input.clientY = GET_Y_LPARAM(lparam);
    input.deltaY = message == WM_MOUSEWHEEL ? -GET_WHEEL_DELTA_WPARAM(wparam) : 0;
    bl_ArcRotateInputEffects effects = {};
    if (bl_processArcRotateInput(host.model.control, &input, &effects) == BL_OK)
    {
        if (effects.capturePointer)
        {
            SetCapture(host.window);
        }
        if (effects.releasePointer)
        {
            ReleaseCapture();
        }
    }
}

static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    auto* host = reinterpret_cast<Host*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        host = static_cast<Host*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        host->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(host));
    }
    if (!host)
    {
        return DefWindowProcW(window, message, wparam, lparam);
    }
    HDROP dropped = message == WM_DROPFILES ? reinterpret_cast<HDROP>(wparam) : nullptr;
    try
    {
        if (message == WM_DROPFILES)
        {
            HDROP drop = dropped;
            UINT count = DragQueryFileW(drop, UINT_MAX, nullptr, 0);
            for (UINT i = 0; i < count; ++i)
            {
                UINT length = DragQueryFileW(drop, i, nullptr, 0);
                std::wstring path(static_cast<size_t>(length) + 1, L'\0');
                DragQueryFileW(drop, i, path.data(), length + 1);
                path.resize(length);
                host->pending.push_back(std::move(path));
            }
            DragFinish(drop);
            dropped = nullptr;
            return 0;
        }
        if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ||
            message == WM_MOUSEMOVE || message == WM_MOUSEWHEEL)
        {
            pointer(*host, message, wparam, lparam);
            return 0;
        }
        if (message == WM_CLOSE)
        {
            host->quit = true;
            return 0;
        }
    }
    catch (...)
    {
        if (dropped)
        {
            DragFinish(dropped);
        }
        SetWindowTextW(window, L"Babylon Lite sandbox | host input allocation/error; try another file");
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static void syntheticDrop(HWND window, const std::wstring& path)
{
    size_t bytes = sizeof(DROPFILES) + (path.size() + 2) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GHND, bytes);
    if (!memory)
    {
        throw std::bad_alloc();
    }
    auto* drop = static_cast<DROPFILES*>(GlobalLock(memory));
    drop->pFiles = sizeof(DROPFILES);
    drop->fWide = TRUE;
    memcpy(reinterpret_cast<uint8_t*>(drop) + sizeof(DROPFILES), path.c_str(),
            (path.size() + 1) * sizeof(wchar_t));
    GlobalUnlock(memory);
    SendMessageW(window, WM_DROPFILES, reinterpret_cast<WPARAM>(memory), 0);
}

static std::string jsonString(const std::string& text)
{
    std::ostringstream stream;
    stream << '"';
    for (unsigned char value : text)
    {
        if (value == '\\' || value == '"')
        {
            stream << '\\' << value;
        }
        else if (value < 32)
        {
            stream << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(value);
        }
        else
        {
            stream << value;
        }
    }
    stream << '"';
    return stream.str();
}

#if defined(LITE_ASSET_MATERIAL_ORACLE_TEST)
#include "../AssetTests/MaterialOracle.h"
#endif

int wmain(int argc, wchar_t** argv)
{
    Host host;
#if defined(LITE_ASSET_MATERIAL_ORACLE_TEST)
    MaterialOracle oracle;
#endif
    std::filesystem::path smoke;
    std::filesystem::path output;
    wchar_t executable[32768] = {};
    GetModuleFileNameW(nullptr, executable, 32768);
    host.assets = std::filesystem::path(executable).parent_path() / "Assets";
    for (int i = 1; i < argc; ++i)
    {
        std::wstring arg = argv[i];
        if ((arg == L"--assets" || arg == L"--smoke-list" || arg == L"--output") && i + 1 < argc)
        {
            std::filesystem::path value = std::filesystem::absolute(argv[++i]);
            if (arg == L"--assets")
            {
                host.assets = value;
            }
            else if (arg == L"--smoke-list")
            {
                smoke = value;
            }
            else
            {
                output = value;
            }
        }
        else if (arg == L"--memory")
        {
            host.memorySource = true;
        }
        else
        {
            host.pending.push_back(std::filesystem::absolute(arg).wstring());
        }
    }
    bool com = false;
    bool gpu = false;
    int result = 0;
    try
    {
        checked(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) ? BL_OK : BL_HOST_ERROR,
                "COM initialization");
        com = true;
        SetProcessDPIAware();
        WNDCLASSW windowClass = {};
        windowClass.lpfnWndProc = windowProcedure;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = L"BabylonLiteNativeGLBSandbox";
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&windowClass);
        DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        RECT rect = {0, 0, Width, Height};
        AdjustWindowRect(&rect, style, FALSE);
        CreateWindowExW(WS_EX_ACCEPTFILES, windowClass.lpszClassName,
                        L"Babylon Lite native GLB sandbox", style, CW_USEDEFAULT, CW_USEDEFAULT,
                        rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr,
                        windowClass.hInstance, &host);
        if (!host.window)
        {
            throw std::runtime_error("Visible native window creation failed");
        }
        DragAcceptFiles(host.window, TRUE);
        ShowWindow(host.window, SW_SHOW);
        UpdateWindow(host.window);
        bgfx::Init init;
        init.type = bgfx::RendererType::Direct3D11;
        init.callback = &host.capture;
        init.swapChain.nwh = host.window;
        init.swapChain.width = Width;
        init.swapChain.height = Height;
        init.swapChain.formatColor = bgfx::TextureFormat::BGRA8;
        init.swapChain.formatDepthStencil = bgfx::TextureFormat::D24S8;
        init.swapChain.flags = BGFX_SWAP_CHAIN_MSAA_X4;
        if (!bgfx::init(init))
        {
            throw std::runtime_error("D3D11/bgfx initialization failed");
        }
        gpu = true;
        bl_Allocator allocator = {&host.memory, allocate, release};
        bl_AssetService assets = {};
        checked(bl_createAssetDecoder(&allocator, &host.decoder, &assets), "Create raw GLB decoder");
        checked(bl_assetDecoderService4(host.decoder, &assets), "Advertise raw GLB geometry inputs");
        host.decoderService = assets;
        assets.userData = &host;
        assets.parseGltf = parseForHost;
        assets.releaseGltf = releaseParsedForHost;
        assets.parseEnvironment = parseEnvironmentForHost;
        assets.releaseEnvironment = releaseEnvironmentForHost;
        auto compiler = bl_shaderCompilerService();
        auto compiler2 = bl_shaderCompilerService2();
#if defined(LITE_ASSET_MATERIAL_ORACLE_TEST)
        compiler2 = materialOracleService(oracle);
#endif
        auto io = SandboxAssetIO();
        bl_RuntimeOptions2 options = {sizeof(options), 2,
                                     {allocator, nullptr, nullptr, &compiler, nullptr, &io},
                                     &assets, &compiler2};
        checked(bl_createRuntime2(&options, &host.runtime), "Create native runtime");
        bl_NativeEngineOptions native = {};
        native.ownership = BL_BGFX_BORROWED;
        native.backend = BL_RENDERER_D3D11;
        native.target = {BL_TARGET_SWAPCHAIN, BL_INVALID_BGFX_HANDLE, Width, Height,
                         BL_COLOR_BGRA8, BL_DEPTH_D24S8, 4, 0, 116};
        native.isExternalBgfxInitialized = initialized;
        native.waitForSubmittedWork = waitSubmitted;
        checked(bl_createEngine(host.runtime, &native, nullptr, &host.engine), "Create native engine");
        checked(bl_startEngine(host.engine, nullptr, nullptr), "Start native engine");
        title(host, "Drag a self-contained static GLB here | left-drag orbit, wheel zoom | close to exit");
        std::deque<std::wstring> automated;
        std::ofstream report;
        if (!smoke.empty())
        {
            if (output.empty())
            {
                throw std::runtime_error("--smoke-list requires --output");
            }
            std::filesystem::create_directories(output);
            std::ifstream list(smoke);
            std::string line;
            while (std::getline(list, line))
            {
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                if (!line.empty())
                {
                    automated.push_back(wide(line));
                }
            }
            if (!list.eof() || automated.empty())
            {
                throw std::runtime_error("Smoke-list file is empty or unreadable");
            }
            report.open(output / "events.jsonl");
        }
        size_t frames = 0;
        size_t sinceDrop = 0;
        size_t item = 0;
        bool capturing = false;
        while (!host.quit)
        {
            MSG message = {};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (!automated.empty() && !capturing && host.pending.empty() && sinceDrop >= 20)
            {
                syntheticDrop(host.window, automated.front());
                automated.pop_front();
                capturing = true;
                sinceDrop = 0;
            }
            if (!host.pending.empty())
            {
                std::wstring path = std::move(host.pending.front());
                host.pending.pop_front();
                loadModel(host, path);
                sinceDrop = 0;
            }
            if (host.model.scene._id)
            {
                checked(bl_frame(host.engine, 1000.0 / 60), "Native frame");
            }
            else
            {
                bgfx::setViewRect(125, 0, 0, Width, Height);
                bgfx::setViewClear(125, BGFX_CLEAR_COLOR, UINT32_C(0x202830ff));
                bgfx::touch(125);
            }
            if (capturing && sinceDrop == 10)
            {
                std::string file = utf8((output / ("drop-" + std::to_string(item) + ".bgra")).wstring());
                host.capture.done = false;
                bgfx::requestScreenShot(BGFX_INVALID_HANDLE, file.c_str());
                bgfx::frame();
                while (!host.capture.done)
                {
                    bgfx::frame();
                }
                if (host.capture.failed)
                {
                    throw std::runtime_error("Actual GPU smoke capture failed");
                }
                bl_EngineStats stats = {};
                checked(bl_getEngineStats(host.engine, &stats), "Actual draw stats");
                report << "{\"item\":" << item << ",\"status\":" << host.loadStatus
                       << ",\"memorySource\":" << (host.memorySource ? "true" : "false")
                       << ",\"success\":" << (host.loaded ? "true" : "false")
                       << ",\"activeAssetId\":" << host.model.asset._id
                       << ",\"activeFile\":" << jsonString(utf8(host.model.path))
                       << ",\"message\":" << jsonString(host.message)
                       << ",\"draws\":" << stats.drawCallCount
                       << ",\"sourceMeshes\":" << host.model.sourceMeshes
                       << ",\"sourcePrimitives\":" << host.model.sourcePrimitives
                       << ",\"sourceVertices\":" << host.model.sourceVertices
                       << ",\"sourceMaterials\":" << host.model.sourceMaterials
                       << ",\"selectedScene\":" << host.model.selectedScene
                       << ",\"selectedNodes\":" << host.model.selectedNodes
                       << ",\"selectedPrimitives\":" << host.model.selectedPrimitives
                       << ",\"emptySceneCamera\":" << (host.model.emptyCamera ? "true" : "false")
                       << ",\"visible\":" << (IsWindowVisible(host.window) ? "true" : "false")
                       << ",\"allocatorLiveBytes\":" << host.memory.bytes << "}\n";
                report.flush();
                ++item;
                capturing = false;
                if (automated.empty())
                {
                    host.quit = true;
                }
            }
            else
            {
                bgfx::frame();
            }
            ++frames;
            ++sinceDrop;
            Sleep(10);
        }
        std::printf("Sandbox frames=%zu successfulDrops=%zu failedDrops=%zu\n",
                     frames, host.successfulDrops, host.failedDrops);
        disposeModel(host, host.model);
        checked(bl_disposeRuntime(host.runtime), "Runtime teardown");
#if defined(LITE_ASSET_MATERIAL_ORACLE_TEST)
        if (oracle.calls != oracle.releases)
        {
            throw std::runtime_error("Every observed shader callback must release exactly once");
        }
#endif
        host.runtime = nullptr;
        bl_destroyAssetDecoder(host.decoder);
        host.decoder = nullptr;
        if (host.memory.live || host.memory.bytes)
        {
            throw std::runtime_error("Runtime/decoder allocator did not release all owned bytes");
        }
        if (!output.empty())
        {
            std::ofstream final(output / "teardown.json");
            final << "{\"runtimeDecoderAllocatorLiveBytes\":0,\"successfulDrops\":"
                  << host.successfulDrops << ",\"failedDrops\":" << host.failedDrops
                  << ",\"visibleWindow\":true,\"actualWM_DROPFILES\":true,"
                     "\"interactiveWithoutSmokeFlagUntilClose\":true}\n";
        }
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "Sandbox: %s\n", error.what());
        if (host.window && smoke.empty())
        {
            MessageBoxW(host.window, wide(error.what()).c_str(), L"Babylon Lite sandbox error", MB_OK);
        }
        result = 2;
        if (host.runtime)
        {
            bl_disposeRuntime(host.runtime);
        }
        if (host.decoder)
        {
            bl_destroyAssetDecoder(host.decoder);
        }
    }
    if (gpu)
    {
        bgfx::shutdown();
    }
    if (host.window)
    {
        DestroyWindow(host.window);
    }
    if (com)
    {
        CoUninitialize();
    }
    return result;
}
