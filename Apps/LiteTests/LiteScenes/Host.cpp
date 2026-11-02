#include "AssetIO.h"
#include "Scene.h"

#include <babylon_lite_asset_decoder.h>
#include <babylon_lite_shader_compiler.h>
#include <bgfx/bgfx.h>
#include <windows.h>
#include <objbase.h>
#include <psapi.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <malloc.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <semaphore>
#include <set>
#include <stdexcept>
#include <vector>
#include <thread>
#ifdef LITE_SCENES_RENDERDOC
#include <renderdoc_app.h>
#endif
#ifdef LITE_SCENES_JS
#include <Babylon/AppRuntime.h>
#include <Babylon/Polyfills/Console.h>
#include <Babylon/Plugins/LiteJSBinding.h>
#include <Babylon/ScriptLoader.h>
#endif

namespace
{
    using Clock = std::chrono::steady_clock;

    double Ms(Clock::duration value)
    {
        return std::chrono::duration<double, std::milli>(value).count();
    }

    uint64_t ProcessCpu()
    {
        FILETIME created{};
        FILETIME exited{};
        FILETIME kernel{};
        FILETIME user{};
        if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        {
            throw std::runtime_error("Process CPU query failed");
        }
        ULARGE_INTEGER k{};
        ULARGE_INTEGER u{};
        k.LowPart = kernel.dwLowDateTime;
        k.HighPart = kernel.dwHighDateTime;
        u.LowPart = user.dwLowDateTime;
        u.HighPart = user.dwHighDateTime;
        return k.QuadPart + u.QuadPart;
    }

    struct Capture : bgfx::CallbackI
    {
        std::atomic<bool> done{};
        std::atomic<bool> failed{};
        uint32_t colorSamples{};
        uint32_t depthSamples{};

        void fatal(const char*, uint16_t, bgfx::Fatal::Enum, const char* text) override
        {
            std::cerr << "GPU fatal: " << text << '\n';
            std::_Exit(3);
        }

        void traceVargs(const char*, uint16_t, const char*, va_list) override
        {
        }

        void profilerBegin(const char*, uint32_t, const char*, uint16_t) override
        {
        }

        void profilerBeginLiteral(const char*, uint32_t, const char*, uint16_t) override
        {
        }

        void profilerEnd() override
        {
        }

        uint32_t cacheReadSize(uint64_t) override
        {
            return 0;
        }

        bool cacheRead(uint64_t, void*, uint32_t) override
        {
            return false;
        }

        void cacheWrite(uint64_t, const void*, uint32_t) override
        {
        }

        void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool) override
        {
        }

        void captureEnd() override
        {
        }

        void captureFrame(const void*, uint32_t) override
        {
        }

        void screenShot(const char* path, uint32_t width, uint32_t height, uint32_t pitch,
                        bgfx::TextureFormat::Enum format, const void* data, uint32_t,
                        bool flip) override
        {
            using Microsoft::WRL::ComPtr;
            auto* device = static_cast<ID3D11Device*>(bgfx::getInternalData()->context);
            ComPtr<ID3D11DeviceContext> context;
            device->GetImmediateContext(&context);
            ComPtr<ID3D11RenderTargetView> color;
            ComPtr<ID3D11DepthStencilView> depth;
            context->OMGetRenderTargets(1, &color, &depth);
            if (color)
            {
                ComPtr<ID3D11Resource> resource;
                ComPtr<ID3D11Texture2D> texture;
                color->GetResource(&resource);
                if (SUCCEEDED(resource.As(&texture)))
                {
                    D3D11_TEXTURE2D_DESC description{};
                    texture->GetDesc(&description);
                    colorSamples = description.SampleDesc.Count;
                }
            }
            if (depth)
            {
                ComPtr<ID3D11Resource> resource;
                ComPtr<ID3D11Texture2D> texture;
                depth->GetResource(&resource);
                if (SUCCEEDED(resource.As(&texture)))
                {
                    D3D11_TEXTURE2D_DESC description{};
                    texture->GetDesc(&description);
                    depthSamples = description.SampleDesc.Count;
                }
            }
            if (!data || width != 1280 || height != 720 || pitch < width * 4 ||
                format != bgfx::TextureFormat::BGRA8)
            {
                failed = true;
                done = true;
                return;
            }
            std::ofstream file(path, std::ios::binary);
            const auto* bytes = static_cast<const char*>(data);
            for (uint32_t y = 0; y < height; ++y)
            {
                file.write(bytes + (flip ? height - 1 - y : y) * pitch, width * 4);
            }
            failed = !file.good();
            done = true;
        }
    };

    struct Sample
    {
        double wall{};
        double events{};
        double core{};
        double submit{};
        uint32_t gpuFrame{};
        int64_t gpuBegin{};
        int64_t gpuEnd{};
        int64_t gpuFrequency{};
        int64_t cpuBegin{};
        int64_t cpuEnd{};
        int64_t cpuFrequency{};
        int64_t waitRender{};
        int64_t waitSubmit{};
    };

    struct Host
    {
        HWND window{};
        bool initialized{};
        bool ownsCom{};
        uint32_t id{};
        uint32_t warmup{120};
        uint32_t measure{};
        bool screenshot{};
        bool minimalTiming{};
        bl_Runtime* runtime{};
        bl_AssetDecoder* decoder{};
        bl_NativeEngineOptions native{};
        bl_ShaderCompilerService compiler{};
        bl_ShaderCompilerService2 compiler2{};
        bl_EngineContext engine{};
        LiteScenes::Assets assets;
        Capture capture;
        bgfx::TextureHandle marker = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle staging = BGFX_INVALID_HANDLE;
        uint8_t markerBytes[4]{};
        std::filesystem::path data{LITE_SCENES_DATA};
        std::filesystem::path output;
        std::filesystem::path script;
        std::filesystem::path renderdocPath;
#ifdef LITE_SCENES_RENDERDOC
        HMODULE renderdocDll{};
        RENDERDOC_API_1_6_0* renderdoc{};
#endif
        std::vector<Sample> samples;
        Clock::time_point created{Clock::now()};
        double setupMs{};
        double firstCompletedMs{};
        double shaderMs{};
        uint64_t shaderCalls{};
        uint64_t measuredCpu{};
        uint32_t draws{};
        uint64_t sceneDraws{};

        bl_Status Wait()
        {
            bgfx::TextureRegion from;
            from.init(marker);
            bgfx::TextureRegion to;
            to.init(staging);
            bgfx::blit(126, to, from);
            const uint32_t ready = bgfx::read(to, markerBytes);
            uint32_t frame = bgfx::frame();
            while (static_cast<int32_t>(frame - ready) < 0)
            {
                frame = bgfx::frame();
            }
            return markerBytes[0] == 19 && markerBytes[1] == 37 ? BL_OK : BL_HOST_ERROR;
        }

        void Initialize()
        {
#ifdef LITE_SCENES_RENDERDOC
            if (!renderdocPath.empty())
            {
                renderdocDll = LoadLibraryW(L"C:\\Program Files\\RenderDoc\\renderdoc.dll");
                const auto getApi = renderdocDll
                                        ? reinterpret_cast<pRENDERDOC_GetAPI>(
                                              GetProcAddress(renderdocDll, "RENDERDOC_GetAPI"))
                                        : nullptr;
                void* api{};
                if (!getApi || !getApi(eRENDERDOC_API_Version_1_6_0, &api))
                {
                    throw std::runtime_error("RenderDoc API unavailable");
                }
                renderdoc = static_cast<RENDERDOC_API_1_6_0*>(api);
                renderdoc->SetCaptureFilePathTemplate(renderdocPath.string().c_str());
            }
#else
            if (!renderdocPath.empty())
            {
                throw std::runtime_error("Build with the optional RenderDoc header for --rdc");
            }
#endif
            LiteScenes::ReadMap(assets, data / ("scene" + std::to_string(id) + ".tsv"));
            const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            ownsCom = SUCCEEDED(com);
            if (FAILED(com) && com != RPC_E_CHANGED_MODE)
            {
                throw std::runtime_error("COM initialization failed");
            }
            window =
                CreateWindowExW(0, L"STATIC", L"Babylon Lite scene harness", WS_POPUP, 0, 0, 1280,
                                720, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (!window)
            {
                throw std::runtime_error("Window creation failed");
            }
            bgfx::Init init;
            init.type = bgfx::RendererType::Direct3D11;
            init.callback = &capture;
            init.swapChain.nwh = window;
            init.swapChain.width = 1280;
            init.swapChain.height = 720;
            init.swapChain.formatColor = bgfx::TextureFormat::BGRA8;
            init.swapChain.formatDepthStencil = bgfx::TextureFormat::D32F;
            init.swapChain.flags = BGFX_SWAP_CHAIN_MSAA_X4;
            if (!bgfx::init(init))
            {
                throw std::runtime_error("D3D11 initialization failed");
            }
            initialized = true;
            const uint8_t pixel[4] = {19, 37, 53, 255};
            marker = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0,
                                           bgfx::copy(pixel, 4));
            staging = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8,
                                            BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST);
            const bl_Allocator allocator{
                nullptr,
                [](void*, size_t bytes, size_t alignment) {
                    return _aligned_malloc(bytes, alignment);
                },
                [](void*, void* pointer, size_t, size_t) { _aligned_free(pointer); }};
            bl_AssetService assetService{};
            LiteScenes::Check(bl_createAssetDecoder(&allocator, &decoder, &assetService));
            compiler = bl_shaderCompilerService();
            compiler2 = bl_shaderCompilerService2();
            auto timedCompiler = compiler;
            timedCompiler.userData = this;
            timedCompiler.compile = [](void* user, const bl_ShaderCompileRequest* request,
                                       bl_ShaderCompileResult* result) {
                auto& host = *static_cast<Host*>(user);
                const auto begin = Clock::now();
                const auto status = host.compiler.compile(host.compiler.userData, request, result);
                host.shaderMs += Ms(Clock::now() - begin);
                ++host.shaderCalls;
                return status;
            };
            timedCompiler.release = [](void* user, bl_ShaderCompileResult* result) {
                auto& host = *static_cast<Host*>(user);
                host.compiler.release(host.compiler.userData, result);
            };
            auto timedCompiler2 = compiler2;
            timedCompiler2.userData = this;
            timedCompiler2.compile = [](void* user, const bl_ShaderCompileRequest2* request,
                                        bl_ShaderCompileResult2* result) {
                auto& host = *static_cast<Host*>(user);
                const auto begin = Clock::now();
                const auto status =
                    host.compiler2.compile(host.compiler2.userData, request, result);
                host.shaderMs += Ms(Clock::now() - begin);
                ++host.shaderCalls;
                return status;
            };
            timedCompiler2.release = [](void* user, bl_ShaderCompileResult2* result) {
                auto& host = *static_cast<Host*>(user);
                host.compiler2.release(host.compiler2.userData, result);
            };
            const auto io = LiteScenes::IO(&assets);
            const bl_RuntimeOptions2 options{
                sizeof(options),
                2,
                {allocator, nullptr, nullptr, &timedCompiler, nullptr, &io},
                &assetService,
                &timedCompiler2};
            LiteScenes::Check(bl_createRuntime2(&options, &runtime));
            LiteScenes::Check(bl_setGltfImportMode(runtime, BL_GLTF_IMPORT_NATIVE));
            native.ownership = BL_BGFX_BORROWED;
            native.backend = BL_RENDERER_D3D11;
            native.target = {BL_TARGET_SWAPCHAIN,
                             BL_INVALID_BGFX_HANDLE,
                             1280,
                             720,
                             BL_COLOR_BGRA8,
                             BL_DEPTH_D32F,
                             4,
                             0,
                             124};
            native.hostUserData = this;
            native.isExternalBgfxInitialized = [](void* user) {
                return static_cast<Host*>(user)->initialized;
            };
            native.waitForSubmittedWork = [](void* user) {
                return static_cast<Host*>(user)->Wait();
            };
            samples.resize(measure);
        }

        void Pump()
        {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }

        void Loop(const std::function<void()>& frame)
        {
#ifdef LITE_SCENES_RENDERDOC
            if (renderdoc)
            {
                renderdoc->StartFrameCapture(nullptr, nullptr);
                frame();
                bgfx::frame();
                LiteScenes::Check(Wait());
                if (!renderdoc->EndFrameCapture(nullptr, nullptr))
                {
                    throw std::runtime_error("RenderDoc end failed");
                }
            }
#endif
            const auto cpu = ProcessCpu();
            const auto shaderCallsBefore = shaderCalls;
            const auto readsBefore = assets.reads;
            const auto decodesBefore = assets.decodes;
            const uint32_t remainingWarmup = warmup ? warmup - 1 : 0;
            for (uint32_t i = 0; i < remainingWarmup + measure; ++i)
            {
                if (i == remainingWarmup)
                {
                    measuredCpu = ProcessCpu();
                }
                const auto begin = Clock::now();
                Pump();
                const auto coreBegin = minimalTiming ? begin : Clock::now();
                frame();
                const auto coreEnd = minimalTiming ? begin : Clock::now();
                bgfx::frame();
                const auto end = Clock::now();
                draws = bgfx::getStats()->numDraw;
                if (i >= remainingWarmup && measure)
                {
                    const auto* stats = bgfx::getStats();
                    samples[i - remainingWarmup] = {
                        Ms(end - begin),   Ms(coreBegin - begin), Ms(coreEnd - coreBegin),
                        Ms(end - coreEnd), stats->gpuFrameNum,    stats->gpuTimeBegin,
                        stats->gpuTimeEnd, stats->gpuTimerFreq,   stats->cpuTimeBegin,
                        stats->cpuTimeEnd, stats->cpuTimerFreq,   stats->waitRender,
                        stats->waitSubmit};
                    draws = stats->numDraw;
                }
            }
            measuredCpu = measure ? ProcessCpu() - measuredCpu : ProcessCpu() - cpu;
            if (measure && (shaderCalls != shaderCallsBefore || assets.reads != readsBefore ||
                            assets.decodes != decodesBefore))
            {
                throw std::runtime_error("Shader compilation or asset IO occurred in timed frames");
            }
            if (screenshot)
            {
                bgfx::requestScreenShot(BGFX_INVALID_HANDLE,
                                        (output / "frame.bgra").string().c_str());
                for (uint32_t i = 0; !capture.done && i < 32; ++i)
                {
                    frame();
                    bgfx::frame();
                }
                LiteScenes::Check(Wait());
                if (!capture.done || capture.failed)
                {
                    throw std::runtime_error("Screenshot failed");
                }
            }
        }

        void Receipt()
        {
            PROCESS_MEMORY_COUNTERS_EX memory{};
            memory.cb = sizeof(memory);
            if (!GetProcessMemoryInfo(GetCurrentProcess(),
                                      reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                                      sizeof(memory)))
            {
                throw std::runtime_error("Memory query failed");
            }
            std::ofstream file(output / "run.json");
            file << std::setprecision(17) << "{\"scene\":" << id << ",\"language\":\""
#ifdef LITE_SCENES_JS
                 << "js"
#else
                 << "native"
#endif
                 << "\",\"backend\":\"bgfx/D3D11\",\"width\":1280,\"height\":720,"
                    "\"requestedMSAA\":4,\"effectiveMSAA\":";
            if (capture.colorSamples && capture.colorSamples == capture.depthSamples)
            {
                file << capture.colorSamples;
            }
            else
            {
                file << "null";
            }
            file << ",\"observedColorSamples\":" << capture.colorSamples
                 << ",\"observedDepthSamples\":" << capture.depthSamples
                 << ",\"depth\":\"D32F\","
                    "\"firstPresentMs\":null,\"setupFromMainMs\":"
                 << setupMs << ",\"firstGpuCompletedFrameFromMainMs\":" << firstCompletedMs
                 << ",\"shaderCompileMs\":" << shaderMs << ",\"shaderCompileCalls\":" << shaderCalls
                 << ",\"rawFileReadMs\":" << assets.readMs
                 << ",\"imageDecodeMs\":" << assets.decodeMs << ",\"rawBytes\":" << assets.bytes
                 << ",\"bgfxDrawCallsIncludingClear\":" << draws << ",\"drawCalls\":" << sceneDraws
                 << ",\"warmup\":" << warmup << ",\"measure\":" << measure
                 << ",\"minimalTiming\":" << (minimalTiming ? "true" : "false")
                 << ",\"fixedDeltaMs\":16.666666666666668,\"physicalInput\":false,"
                    "\"measuredCaptureOrFileIO\":false,\"processCpuMs\":"
                 << double(measuredCpu) / 10000 << ",\"privateBytes\":" << memory.PrivateUsage
                 << ",\"samples\":[";
            std::set<uint32_t> ids;
            for (size_t i = 0; i < samples.size(); ++i)
            {
                const auto& s = samples[i];
                if (i)
                {
                    file << ',';
                }
                file << "{\"completeHostWallMs\":" << s.wall;
                if (!minimalTiming)
                {
                    file << ",\"eventsMs\":" << s.events << ",\"coreAndCallbacksMs\":" << s.core
                         << ",\"submitAndWaitMs\":" << s.submit;
                }
                file << ",\"renderThreadCpuMs\":";
                if (s.cpuFrequency > 0 && s.cpuEnd >= s.cpuBegin)
                {
                    file << double(s.cpuEnd - s.cpuBegin) * 1000 / double(s.cpuFrequency);
                }
                else
                {
                    file << "null";
                }
                file << ",\"waitRenderTicks\":" << s.waitRender
                     << ",\"waitSubmitTicks\":" << s.waitSubmit << ",\"gpu\":";
                if (s.gpuFrequency > 0 && s.gpuEnd > s.gpuBegin && ids.insert(s.gpuFrame).second)
                {
                    file << "{\"id\":" << s.gpuFrame << ",\"ms\":"
                         << double(s.gpuEnd - s.gpuBegin) * 1000 / double(s.gpuFrequency) << '}';
                }
                else
                {
                    file << "null";
                }
                file << '}';
            }
            file << "]}\n";
            if (!file)
            {
                throw std::runtime_error("Run receipt failed");
            }
        }

        void Finish()
        {
            if (runtime)
            {
                LiteScenes::Check(bl_disposeRuntime(runtime));
                runtime = nullptr;
            }
            if (decoder)
            {
                bl_destroyAssetDecoder(decoder);
                decoder = nullptr;
            }
            if (initialized)
            {
                bgfx::destroy(marker);
                bgfx::destroy(staging);
                bgfx::frame();
                bgfx::shutdown();
                initialized = false;
            }
            if (window)
            {
                DestroyWindow(window);
                window = nullptr;
            }
            if (ownsCom)
            {
                CoUninitialize();
                ownsCom = false;
            }
#ifdef LITE_SCENES_RENDERDOC
            if (renderdocDll)
            {
                FreeLibrary(renderdocDll);
                renderdocDll = nullptr;
                renderdoc = nullptr;
            }
#endif
        }
    };

    Host* current{};
}

namespace LiteScenes
{
    void Check(bl_Status status)
    {
        if (status == BL_OK)
        {
            return;
        }
        bl_Error error{};
        if (current && current->runtime)
        {
            bl_getLastError(current->runtime, &error);
        }
        throw std::runtime_error(
            "C99 status " + std::to_string(status) + ": " +
            (error.message.data ? std::string(error.message.data, error.message.length) : ""));
    }
}

int main(int argc, char** argv)
{
    Host host;
    current = &host;
    try
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg.starts_with("--scene="))
            {
                host.id = static_cast<uint32_t>(std::stoul(arg.substr(8)));
            }
            else if (arg.starts_with("--data="))
            {
                host.data = arg.substr(7);
            }
            else if (arg.starts_with("--out="))
            {
                host.output = arg.substr(6);
            }
            else if (arg.starts_with("--script="))
            {
                host.script = arg.substr(9);
            }
            else if (arg.starts_with("--rdc="))
            {
                host.renderdocPath = arg.substr(6);
            }
            else if (arg.starts_with("--warmup="))
            {
                host.warmup = static_cast<uint32_t>(std::stoul(arg.substr(9)));
            }
            else if (arg.starts_with("--measure="))
            {
                host.measure = static_cast<uint32_t>(std::stoul(arg.substr(10)));
            }
            else if (arg == "--capture")
            {
                host.screenshot = true;
            }
            else if (arg == "--minimal-timing")
            {
                host.minimalTiming = true;
            }
            else
            {
                throw std::runtime_error("Unknown host option: " + arg);
            }
        }
        if (!host.id || host.output.empty() ||
            (host.measure && (host.screenshot || !host.renderdocPath.empty())))
        {
            throw std::runtime_error("--scene and --out required; capture and timing are separate");
        }
        std::filesystem::create_directories(host.output);
#ifdef LITE_SCENES_JS
        std::string error;
        Babylon::AppRuntime::Options vmOptions{};
        vmOptions.UnhandledExceptionHandler = [&](const Napi::Error& value) {
            error = Napi::GetErrorString(value);
        };
        Babylon::AppRuntime vm(vmOptions);
        Babylon::ScriptLoader loader(vm);
        std::binary_semaphore complete{0};
        std::exception_ptr failure;
        const auto dispatch = [&](const std::function<void(Napi::Env)>& action) {
            vm.Dispatch([&](Napi::Env env) {
                try
                {
                    action(env);
                }
                catch (...)
                {
                    failure = std::current_exception();
                }
                complete.release();
            });
            complete.acquire();
            if (failure)
            {
                std::rethrow_exception(failure);
            }
        };
        dispatch([&](Napi::Env env) {
            host.Initialize();
            Babylon::Polyfills::Console::Initialize(
                env, [&](const char* text, Babylon::Polyfills::Console::LogLevel level) {
                    if (level == Babylon::Polyfills::Console::LogLevel::Error)
                    {
                        error = text;
                    }
                });
            Babylon::Plugins::LiteJSBinding::Initialize(env, {host.runtime, host.native});
            auto performance = Napi::Object::New(env);
            performance.Set("now", Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                                return Napi::Number::New(info.Env(),
                                                         Ms(Clock::now().time_since_epoch()));
                            }));
            env.Global().Set("performance", performance);
            env.RunScript(R"JS(
globalThis.window=globalThis;
globalThis._sceneHostEngine=null;
const _sceneCreateEngine=_liteNative.createEngine;
_liteNative.createEngine=(...args)=>{const engine=_sceneCreateEngine(...args);
    _sceneHostEngine=engine;return engine;};
globalThis._sceneHostStarted=false;
const _sceneStartEngine=_liteNative.startEngine;
_liteNative.startEngine=(...args)=>{const result=_sceneStartEngine(...args);
    _sceneHostStarted=true;return result;};
globalThis._canvas={width:1280,height:720,dataset:{},style:{},
    addEventListener(){},removeEventListener(){},setPointerCapture(){},releasePointerCapture(){},
    getBoundingClientRect(){return {left:0,top:0,width:1280,height:720};}};
globalThis.document={getElementById:()=>_canvas,querySelector:()=>_canvas};
)JS");
        });
        std::ifstream stream(host.script.empty()
                                 ? host.data / ("scene" + std::to_string(host.id) + ".js")
                                 : host.script);
        if (!stream)
        {
            throw std::runtime_error("Missing native-JS bundle");
        }
        const std::string script{std::istreambuf_iterator<char>(stream), {}};
        loader.Eval(script, "app:///scene.ts");
        bool ready{};
        bool firstFrame{};
        for (unsigned i = 0; !ready && i < 100; ++i)
        {
            dispatch([&](Napi::Env env) {
                if (!firstFrame && env.Global().Get("_sceneHostStarted").ToBoolean().Value())
                {
                    Babylon::Plugins::LiteJSBinding::Frame(env, 1000.0 / 60);
                    bgfx::frame();
                    firstFrame = true;
                }
                const auto dataset = env.Global()
                                         .Get("_canvas")
                                         .As<Napi::Object>()
                                         .Get("dataset")
                                         .As<Napi::Object>();
                ready = dataset.Get("ready").ToBoolean().Value();
                if (dataset.Has("error"))
                {
                    error = dataset.Get("error").ToString().Utf8Value();
                }
            });
            if (!error.empty())
            {
                throw std::runtime_error(error);
            }
            if (!ready)
            {
                std::this_thread::yield();
            }
        }
        if (!ready)
        {
            throw std::runtime_error("Original JS did not finish setup");
        }
        dispatch([&](Napi::Env env) {
            host.setupMs = Ms(Clock::now() - host.created);
            LiteScenes::Check(host.Wait());
            host.firstCompletedMs = Ms(Clock::now() - host.created);
            host.Loop([&]() { Babylon::Plugins::LiteJSBinding::Frame(env, 1000.0 / 60); });
            const auto api = env.Global().Get("_liteNative").As<Napi::Object>();
            const auto stats = api.Get("getEngineStats")
                                   .As<Napi::Function>()
                                   .Call({env.Global().Get("_sceneHostEngine")})
                                   .As<Napi::Object>();
            host.sceneDraws =
                static_cast<uint64_t>(stats.Get("drawCallCount").As<Napi::Number>().DoubleValue());
            if (!error.empty())
            {
                throw std::runtime_error(error);
            }
            host.Receipt();
            LiteScenes::Check(bl_disposeRuntime(host.runtime));
            host.runtime = nullptr;
            Babylon::Plugins::LiteJSBinding::Dispose(env);
            host.Finish();
        });
#else
        host.Initialize();
        const bl_EngineOptions options{BL_BOOL_FALSE};
        LiteScenes::Check(bl_createEngine(host.runtime, &host.native, &options, &host.engine));
        LiteScenes::CreateScene(host.id, host.runtime, host.engine);
        LiteScenes::Check(bl_startEngine(host.engine, nullptr, nullptr));
        host.setupMs = Ms(Clock::now() - host.created);
        LiteScenes::Check(bl_frame(host.engine, 1000.0 / 60));
        bgfx::frame();
        LiteScenes::Check(host.Wait());
        host.firstCompletedMs = Ms(Clock::now() - host.created);
        host.Loop([&]() { LiteScenes::Check(bl_frame(host.engine, 1000.0 / 60)); });
        bl_EngineStats stats{};
        LiteScenes::Check(bl_getEngineStats(host.engine, &stats));
        host.sceneDraws = stats.drawCallCount;
        host.Receipt();
        host.Finish();
#endif
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
