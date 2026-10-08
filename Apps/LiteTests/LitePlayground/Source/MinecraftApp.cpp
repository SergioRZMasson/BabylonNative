#include "NativePlatform.h"
#include "NativeAudio.h"
#include "FrameReceipt.h"
#include "Diagnostics.h"
#include <Babylon/AppRuntime.h>
#include <Babylon/ScriptLoader.h>
#include <Babylon/Plugins/LiteJSBinding.h>
#include <Babylon/Polyfills/Console.h>
#include <babylon_lite_shader_compiler.h>
#include <bgfx/bgfx.h>
#include <malloc.h>
#include <atomic>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <future>
#include <memory>
#include <string>
#include <unordered_set>
#if defined(LITE_V8_GC_TEST)
#include <v8.h>
#endif

namespace
{
    LitePlayground::NativePlatform* s_platform{};

    LRESULT CALLBACK MinecraftWindow(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (s_platform && s_platform->Message(message, wParam, lParam))
        {
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
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

    void Wait(std::future<void>& future)
    {
        const auto start = std::chrono::steady_clock::now();
        while (future.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready)
        {
            Pump();
            if (std::chrono::steady_clock::now() - start > std::chrono::minutes(10))
            {
                throw std::runtime_error("Full Minecraft JS dispatch exceeded 10 minutes.");
            }
        }
        future.get();
    }

    std::string Script(const char* name)
    {
        std::ifstream stream(std::filesystem::path(LITE_SCRIPT_DIRECTORY) / (std::string(name) + ".native.js"), std::ios::binary);
        if (!stream)
        {
            throw std::runtime_error("Missing original application bundle.");
        }
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    struct Graphics : bgfx::CallbackI
    {
        bool initialized{};
        std::atomic<uint32_t> captures{};
        std::atomic<bool> captureFailed{};
        std::atomic<uint64_t> lastCaptureHash{};
        std::atomic<uint64_t> firstCaptureHash{};
        bgfx::TextureHandle fence{BGFX_INVALID_HANDLE};
        std::array<uint8_t, 4> fenceBytes{};

        void fatal(const char* file, uint16_t line, bgfx::Fatal::Enum, const char* text) override
        {
            Diagnostics::DumpFailure("BGFX FATAL", file, line, 0, "%s", text);
            Diagnostics::SetExitCode(3);
            std::_Exit(3);
        }
        void traceVargs(const char*, uint16_t, const char* format, va_list arguments) override
        {
            char text[2048]{};
            vsnprintf(text, sizeof(text), format, arguments);
            OutputDebugStringA(text);
        }
        void profilerBegin(const char*, uint32_t, const char*, uint16_t) override {}
        void profilerBeginLiteral(const char*, uint32_t, const char*, uint16_t) override {}
        void profilerEnd() override {}
        uint32_t cacheReadSize(uint64_t) override { return 0; }
        bool cacheRead(uint64_t, void*, uint32_t) override { return false; }
        void cacheWrite(uint64_t, const void*, uint32_t) override {}
        void screenShot(const char* path, uint32_t width, uint32_t height, uint32_t pitch, bgfx::TextureFormat::Enum format,
            const void* data, uint32_t, bool flip) override
        {
            if (!data || width < 1 || height < 1 || pitch < width * 4 || format != bgfx::TextureFormat::BGRA8)
            {
                captureFailed = true;
                return;
            }
            const auto* pixels = static_cast<const uint8_t*>(data);
            uint64_t hash = 1469598103934665603ull;
            uint64_t nonblack{};
            for (uint32_t y = 0; y < height; ++y)
            {
                for (uint32_t x = 0; x < width; ++x)
                {
                    const auto* pixel = pixels + static_cast<size_t>(y) * pitch + x * 4;
                    if (pixel[0] || pixel[1] || pixel[2])
                        ++nonblack;
                    for (uint32_t channel = 0; channel < 3; ++channel)
                    {
                        hash = (hash ^ pixel[channel]) * 1099511628211ull;
                    }
                }
            }
            if (nonblack < static_cast<uint64_t>(width) * height / 10)
            {
                captureFailed = true;
                return;
            }
            if (!captures.load())
                firstCaptureHash = hash;
            lastCaptureHash = hash;
            const bool written = LitePlayground::NativePlatform::WritePNG(std::filesystem::path(path), width, height, pitch,
                static_cast<const uint8_t*>(data), flip);
            if (written)
            {
                ++captures;
                std::printf("Minecraft GPU capture: %s (%ux%u)\n", path, width, height);
            }
            else
            {
                captureFailed = true;
            }
        }
        void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool) override {}
        void captureEnd() override {}
        void captureFrame(const void*, uint32_t) override {}

        static bl_Status WaitSubmitted(void* user)
        {
            auto& graphics = *static_cast<Graphics*>(user);
            bgfx::TextureRegion region{};
            region.handle = graphics.fence;
            const uint32_t ready = bgfx::read(region, graphics.fenceBytes.data());
            for (uint32_t i = 0; i < 32; ++i)
            {
                if (bgfx::frame() >= ready)
                {
                    return BL_OK;
                }
            }
            return BL_HOST_ERROR;
        }
    };
}

int RunMinecraft(int argc, char** argv)
{
    const bool uiFixture = std::string(argv[1]) == "ui-contract";
    constexpr uint32_t width = 1280;
    constexpr uint32_t height = 720;
    uint32_t limit{};
    bool replay{};
    bool hidden{};
    bool windowReplay{};
    bool dialogTest{};
    bool uiValidation{};
    bool fixedStep{};
    uint32_t warmup{180};
    uint32_t measure{1200};
    std::filesystem::path benchmarkOutput;
#if defined(LITE_RETAINED_UI)
    bool retainedUi{true};
#else
    bool retainedUi{};
#endif
    std::filesystem::path captureRoot = std::filesystem::path("build") / "lite-c99" / "js-rmlui" / "runs";
    for (int i = 2; i < argc; ++i)
    {
        const std::string option(argv[i]);
        if (option.starts_with("--frames="))
            limit = static_cast<uint32_t>(std::stoul(option.substr(9)));
        else if (option == "--replay")
            replay = true;
        else if (option == "--headless")
            hidden = true;
        else if (option == "--fixed-step")
            fixedStep = true;
        else if (option == "--legacy-gdi-ui")
            retainedUi = false;
        else if (option == "--ui-validation")
        {
            uiValidation = true;
            replay = true;
            windowReplay = true;
            dialogTest = true;
        }
        else if (option == "--window-input-replay")
        {
            replay = true;
            windowReplay = true;
        }
        else if (option == "--test-save-load")
        {
            replay = true;
            windowReplay = true;
            dialogTest = true;
        }
        else if (option.starts_with("--capture-root="))
            captureRoot = std::filesystem::path(option.substr(15));
        else if (option.starts_with("--benchmark-output="))
            benchmarkOutput = std::filesystem::path(option.substr(19));
        else if (option == "--benchmark-output" && i + 1 < argc)
            benchmarkOutput = std::filesystem::path(argv[++i]);
        else if (option.starts_with("--warmup="))
            warmup = static_cast<uint32_t>(std::stoul(option.substr(9)));
        else if (option == "--warmup" && i + 1 < argc)
            warmup = static_cast<uint32_t>(std::stoul(argv[++i]));
        else if (option.starts_with("--measure="))
            measure = static_cast<uint32_t>(std::stoul(option.substr(10)));
        else if (option == "--measure" && i + 1 < argc)
            measure = static_cast<uint32_t>(std::stoul(argv[++i]));
        else
            throw std::runtime_error("Unknown Minecraft host option: " + option);
    }
    const bool benchmark = !benchmarkOutput.empty();
    if ((uiValidation || uiFixture) && !retainedUi)
    {
        throw std::runtime_error("Native UI validation requires a UI-ON build and retained UI mode.");
    }
    if (benchmark)
    {
        if (replay || dialogTest || uiFixture || uiValidation || !measure || measure > 1000000 || warmup > 1000000)
        {
            throw std::runtime_error("Benchmark protocol rejects replay/dialog/UI overrides and unbounded frame counts.");
        }
        limit = warmup + measure;
        hidden = true;
    }
    std::filesystem::create_directories(captureRoot);
    const wchar_t* name = L"BabylonLiteFullMinecraft";
    constexpr DWORD windowStyle = WS_OVERLAPPEDWINDOW;
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = MinecraftWindow;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = name;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    RegisterClassW(&windowClass);
    RECT bounds{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    AdjustWindowRect(&bounds, windowStyle, FALSE);
    HWND window = CreateWindowW(name, L"Babylon Lite — original Minecraft — SEED 1337 / radius 6", windowStyle,
        CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
        nullptr, nullptr, windowClass.hInstance, nullptr);
    if (!window)
    {
        throw std::runtime_error("Minecraft native window creation failed.");
    }
    if (!hidden)
    {
        ShowWindow(window, SW_SHOW);
        UpdateWindow(window);
    }
    LitePlayground::NativePlatform platform(window, width, height, retainedUi, limit != 0, hidden);
    if (dialogTest)
    {
        platform.ConfigureFileDialogTest(captureRoot / ("world-" + std::to_string(GetCurrentProcessId()) + ".voxelsave.json"));
    }
    s_platform = &platform;
    Graphics graphics;
    LitePlayground::NativeAudio audio;
    bl_Runtime* nativeRuntime{};
    std::atomic<bool> failed{};
    std::string failure;
    uint32_t rendered{};
    std::vector<LitePlayground::FrameSample> samples;
    std::unordered_set<uint32_t> gpuFrames;
    std::string uiBefore;
    std::string uiAfter;
    if (benchmark)
    {
        samples.reserve(measure);
        gpuFrames.reserve(measure);
    }
    bool ready{};
    int result = 1;
    {
        Babylon::AppRuntime::Options options;
        options.UnhandledExceptionHandler = [&](const Napi::Error& error) {
            failure = Napi::GetErrorString(error);
            Diagnostics::DumpFailure("MINECRAFT JS ERROR", nullptr, 0, 0, "%s", failure.c_str());
            failed = true;
        };
        Babylon::AppRuntime runtime(options);
        Babylon::ScriptLoader loader(runtime);
        std::promise<void> initialized;
        auto initDone = initialized.get_future();
        runtime.Dispatch([&](Napi::Env env) {
            try
            {
                Babylon::Polyfills::Console::Initialize(env, [](const char* text, Babylon::Polyfills::Console::LogLevel level) {
                    if (level == Babylon::Polyfills::Console::LogLevel::Error)
                    {
                        Diagnostics::DumpFailure("MINECRAFT CONSOLE", nullptr, 0, 0, "%s", text);
                    }
                    else
                    {
                        std::printf("%s\n", text);
                    }
                });
                platform.Initialize(env, LITE_ASSET_DIRECTORY);
                bgfx::Init device;
                device.type = bgfx::RendererType::Direct3D11;
                device.swapChain.nwh = window;
                device.swapChain.width = width;
                device.swapChain.height = height;
                device.swapChain.formatColor = bgfx::TextureFormat::BGRA8;
                device.swapChain.formatDepthStencil = bgfx::TextureFormat::D24S8;
                device.swapChain.flags = BGFX_SWAP_CHAIN_MSAA_X4;
                device.callback = &graphics;
                if (!bgfx::init(device))
                {
                    throw std::runtime_error("Full Minecraft bgfx initialization failed.");
                }
                graphics.initialized = true;
                graphics.fence = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_READ_BACK);
                if (!bgfx::isValid(graphics.fence))
                {
                    throw std::runtime_error("D3D11 submitted-work readback fence creation failed.");
                }
                const auto compiler = bl_shaderCompilerService();
                bl_RuntimeOptions nativeOptions{};
                nativeOptions.allocator.allocate = [](void*, size_t bytes, size_t alignment) { return _aligned_malloc(bytes, std::max(alignment, sizeof(void*))); };
                nativeOptions.allocator.deallocate = [](void*, void* data, size_t, size_t) { _aligned_free(data); };
                nativeOptions.shaderCompiler = &compiler;
                const auto audioService = audio.Service();
                nativeOptions.audio = &audioService;
                if (bl_createRuntime(&nativeOptions, &nativeRuntime) != BL_OK)
                {
                    throw std::runtime_error("Full Minecraft native runtime creation failed.");
                }
                Babylon::Plugins::LiteJSBinding::HostOptions binding;
                binding.runtime = nativeRuntime;
                binding.nativeEngine.ownership = BL_BGFX_BORROWED;
                binding.nativeEngine.backend = BL_RENDERER_D3D11;
                binding.nativeEngine.target = {BL_TARGET_SWAPCHAIN, BL_INVALID_BGFX_HANDLE, width, height, BL_COLOR_BGRA8, BL_DEPTH_D24S8, 4, 0, static_cast<uint16_t>(retainedUi ? 32 : 64)};
                binding.nativeEngine.hostUserData = &graphics;
                binding.nativeEngine.isExternalBgfxInitialized = [](void* user) { return static_cast<Graphics*>(user)->initialized; };
                binding.nativeEngine.waitForSubmittedWork = Graphics::WaitSubmitted;
                Babylon::Plugins::LiteJSBinding::Initialize(env, binding);
#if defined(LITE_V8_GC_TEST)
                env.Global().Set("_liteForceGC", Napi::Function::New(env, [](const Napi::CallbackInfo&) {
                    v8::Isolate::GetCurrent()->LowMemoryNotification();
                }));
#endif
            }
            catch (const std::exception& error)
            {
                failure = error.what();
                failed = true;
            }
            initialized.set_value();
        });
        Wait(initDone);
        if (!failed)
        {
            try
            {
            loader.Eval(Script("platform"), "app:///Platform/platform.js");
            loader.Eval(Script(uiFixture ? "ui-contract" : "minecraft"),
                        uiFixture ? "app:///Tests/ui-contract.ts" : "app:///Demos/minecraft.ts");
            }
            catch (const std::exception& error)
            {
                failure = error.what();
                failed = true;
            }
        }
        const auto started = std::chrono::steady_clock::now();
        auto previous = started;
        while (!failed && !platform.Closed() && (!limit || rendered < limit))
        {
            const auto loopBegin = std::chrono::steady_clock::now();
            const auto loopExecutionBegin = benchmark ? LitePlayground::ReadExecutionTimes() : LitePlayground::ExecutionTimes{};
            Pump();
            if (ready && windowReplay)
            {
                platform.QueueWindowReplay(rendered + 1);
                Pump();
            }
            if (ready && uiValidation)
            {
                platform.QueueUiValidation(rendered + 1);
                Pump();
            }
            const auto viewport = platform.TakeViewport();
            const auto now = std::chrono::steady_clock::now();
            const double delta = replay || benchmark || fixedStep ? 1000.0 / 60 : std::min(100.0, std::chrono::duration<double, std::milli>(now - previous).count());
            previous = now;
            std::promise<void> framed;
            auto frameDone = framed.get_future();
            runtime.Dispatch([&](Napi::Env env) {
                try
                {
                    if (viewport.changed)
                    {
                        if (Graphics::WaitSubmitted(&graphics) != BL_OK)
                        {
                            throw std::runtime_error("Resize synchronization failed.");
                        }
                        bgfx::SwapChain swapChain;
                        swapChain.width = viewport.width;
                        swapChain.height = viewport.height;
                        bgfx::reset(BGFX_RESET_NONE, &swapChain);
                        if (retainedUi)
                        {
                            env.Global().Get("_platformUiViewport").As<Napi::Function>().Call({
                                Napi::Number::New(env, viewport.width), Napi::Number::New(env, viewport.height),
                                Napi::Number::New(env, viewport.density)});
                        }
                    }
                    const auto frameBegin = std::chrono::steady_clock::now();
                    const auto executionBegin = benchmark ? LitePlayground::ReadExecutionTimes() : LitePlayground::ExecutionTimes{};
                    LitePlayground::FrameSample sample{};
                    platform.DispatchEvents(env);
                    const auto tick = env.Global().Get("_platformTick");
                    if (tick.IsFunction())
                    {
                        ready = tick.As<Napi::Function>().Call({Napi::Number::New(env, delta)}).As<Napi::Boolean>().Value();
                    }
                    if (ready && replay && !windowReplay)
                    {
                        platform.Replay(env, rendered + 1);
                    }
                    const auto engineBegin = std::chrono::steady_clock::now();
                    sample.platformUserMs = std::chrono::duration<double, std::milli>(engineBegin - frameBegin).count();
                    Babylon::Plugins::LiteJSBinding::Frame(env, delta);
                    const auto timings = Babylon::Plugins::LiteJSBinding::GetFrameTimings(env);
                    sample.nativeIncludingCallbacksMs = timings.nativeFrameIncludingCallbacksMs;
                    sample.userCallbacksMs = timings.userCallbacksMs;
                    if (retainedUi && ready)
                    {
                        const auto updateBegin = std::chrono::steady_clock::now();
                        if (uiValidation)
                        {
                            env.Global().Get("_platformUiValidate").As<Napi::Function>().Call({
                                Napi::Number::New(env, rendered + 1)});
                        }
                        const auto milliseconds = env.Global().Get("_platformNow").As<Napi::Function>().Call({});
                        env.Global().Get("_platformUiUpdate").As<Napi::Function>().Call({milliseconds});
                        const auto renderBegin = std::chrono::steady_clock::now();
                        sample.uiUpdateMs = std::chrono::duration<double, std::milli>(renderBegin - updateBegin).count();
                        env.Global().Get("_platformUiRender").As<Napi::Function>().Call({});
                        sample.uiRenderMs = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - renderBegin).count();
                    }
                    if (ready)
                    {
                        ++rendered;
                        if (!benchmark && (rendered == 180 || rendered == 1380 || (replay && rendered == 240) ||
                            (uiFixture && rendered == 10) || (uiValidation &&
                            (rendered == 130 || rendered == 300 || rendered == 380))))
                        {
                            const auto path = captureRoot / (std::string(replay ? "replay-" : "frame-") + std::to_string(rendered) + ".png");
                            bgfx::requestScreenShot(BGFX_INVALID_HANDLE, path.string().c_str());
                            if (!retainedUi && !platform.CaptureHUD(captureRoot / ("hud-" + std::to_string(rendered) + ".png")))
                            {
                                throw std::runtime_error("Native HUD capture failed.");
                            }
                        }
                    }
                    const auto presentBegin = std::chrono::steady_clock::now();
                    bgfx::frame();
                    const auto frameEnd = std::chrono::steady_clock::now();
                    sample.presentMs = std::chrono::duration<double, std::milli>(frameEnd - presentBegin).count();
                    sample.hostApiFrameMs = std::chrono::duration<double, std::milli>(frameEnd - frameBegin).count();
                    if (benchmark && ready)
                    {
                        const auto executionEnd = LitePlayground::ReadExecutionTimes();
                        sample.frame = rendered;
                        sample.processCpuMs = (executionEnd.process - executionBegin.process) / 10000.0;
                        sample.apiThreadCpuMs = (executionEnd.apiThread - executionBegin.apiThread) / 10000.0;
                        const auto* stats = bgfx::getStats();
                        if (stats && stats->cpuTimerFreq > 0 && stats->cpuTimeEnd >= stats->cpuTimeBegin)
                        {
                            sample.renderSubmitWallMs = 1000.0 * (stats->cpuTimeEnd - stats->cpuTimeBegin) / stats->cpuTimerFreq;
                        }
                        if (stats && stats->gpuTimerFreq > 0 && stats->gpuTimeEnd > stats->gpuTimeBegin &&
                            gpuFrames.insert(stats->gpuFrameNum).second)
                        {
                            sample.gpuAvailable = true;
                            sample.gpuFrame = stats->gpuFrameNum;
                            sample.gpuBegin = stats->gpuTimeBegin;
                            sample.gpuEnd = stats->gpuTimeEnd;
                            sample.gpuFrequency = stats->gpuTimerFreq;
                            sample.gpuMs = 1000.0 * (sample.gpuEnd - sample.gpuBegin) / sample.gpuFrequency;
                        }
                        if (rendered > warmup)
                        {
                            samples.push_back(sample);
                        }
                        if (retainedUi && (rendered == warmup || rendered == limit || (!warmup && rendered == 1)))
                        {
                            const auto value = env.Global().Get("_platformUiStats").As<Napi::Function>().Call({});
                            const auto text = env.Global().Get("JSON").As<Napi::Object>().Get("stringify")
                                .As<Napi::Function>().Call({value}).As<Napi::String>().Utf8Value();
                            if (rendered == limit)
                            {
                                uiAfter = text;
                            }
                            else
                            {
                                uiBefore = text;
                            }
                        }
                    }
                }
                catch (const Napi::Error& error)
                {
                    failure = Napi::GetErrorString(error);
                    failed = true;
                }
                catch (const std::exception& error)
                {
                    failure = error.what();
                    failed = true;
                }
                framed.set_value();
            });
            Wait(frameDone);
            if (benchmark && !samples.empty() && samples.back().frame == rendered)
            {
                const auto loopExecutionEnd = LitePlayground::ReadExecutionTimes();
                auto& sample = samples.back();
                sample.completeHostWallMs = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - loopBegin).count();
                sample.processCpuCompleteHostMs = (loopExecutionEnd.process - loopExecutionBegin.process) / 10000.0;
                sample.osThreadCpuMs = (loopExecutionEnd.apiThread - loopExecutionBegin.apiThread) / 10000.0;
            }
            if (!ready && std::chrono::steady_clock::now() - started > std::chrono::minutes(10))
            {
                failed = true;
                failure = "Original full radius-6 Minecraft boot timed out.";
            }
        }
        std::promise<void> disposed;
        auto disposedDone = disposed.get_future();
        runtime.Dispatch([&](Napi::Env env) {
            if (nativeRuntime)
            {
                if (uiFixture && !failed)
                {
                    const auto fixture = env.Global().Get("_liteUiFixtureResult");
                    if (!fixture.IsBoolean() || !fixture.As<Napi::Boolean>().Value())
                    {
                        failed = true;
                        failure = "Native UI fixture did not complete its callback checks.";
                    }
                }
                if (retainedUi && env.Global().Get("_platformUiDispose").IsFunction())
                {
                    try
                    {
                    env.Global().Get("_platformUiDispose").As<Napi::Function>().Call({});
                    }
                    catch (const Napi::Error& error)
                    {
                        failed = true;
                        failure = Napi::GetErrorString(error);
                    }
                }
                const auto status = bl_disposeRuntime(nativeRuntime);
                if (status != BL_OK)
                {
                    Diagnostics::DumpFailure("MINECRAFT RUNTIME DISPOSAL", nullptr, 0, 0,
                        "C99 disposal failed with status %d; live runtime cannot outlive its JS callback userdata.", status);
                    std::_Exit(3);
                }
                try
                {
                    Babylon::Plugins::LiteJSBinding::Dispose(env);
                }
                catch (const Napi::Error& error)
                {
                    failed = true;
                    failure = Napi::GetErrorString(error);
                }
                nativeRuntime = nullptr;
            }
            if (graphics.initialized)
            {
                if (bgfx::isValid(graphics.fence))
                {
                    bgfx::destroy(graphics.fence);
                }
                bgfx::frame();
                bgfx::frame();
                bgfx::shutdown();
                graphics.initialized = false;
            }
            platform.DisposeOnJsThread();
            disposed.set_value();
        });
        Wait(disposedDone);
    }
    s_platform = nullptr;
    const uint32_t requiredCaptures = benchmark ? 0 : limit >= 1380 || (limit >= 240 && replay) ? 2 : limit >= 180 ? 1
                                                                                : 0;
    if (!failed && !platform.AssetFailures() && !graphics.captureFailed && (!limit || rendered >= limit) &&
        graphics.captures >= requiredCaptures && (requiredCaptures < 2 || graphics.firstCaptureHash != graphics.lastCaptureHash) &&
        (!dialogTest || platform.FileSelectionsCompleted() == 2) && (!windowReplay || platform.NativeInputEvents() > 0))
    {
        result = 0;
        std::printf("Full original Minecraft: seed=1337 radius=6 ready=%s frames=%u captures=%u assetFailures=%zu audioFrames=%llu nonzeroAudioSamples=%llu.\n",
            ready ? "true" : "false", rendered, graphics.captures.load(), platform.AssetFailures(),
            static_cast<unsigned long long>(audio.SubmittedFrames()), static_cast<unsigned long long>(audio.NonzeroSamples()));
    }
    else
    {
        Diagnostics::DumpFailure("MINECRAFT RUN FAILURE", nullptr, 0, 0, "%s (frames=%u, assetFailures=%zu)", failure.c_str(), rendered, platform.AssetFailures());
    }
    std::ofstream summary(captureRoot / "summary.json");
    summary << "{\"seed\":1337,\"radius\":6,\"frames\":" << rendered << ",\"ready\":" << (ready ? "true" : "false")
            << ",\"retainedUi\":" << (retainedUi ? "true" : "false")
            << ",\"uiOnlyValidationOverrides\":" << (uiValidation ? "true" : "false")
            << ",\"boundedPhysicalInputRejected\":" << (limit ? "true" : "false")
            << ",\"captures\":" << graphics.captures.load() << ",\"assetFailures\":" << platform.AssetFailures()
            << ",\"audioSubmittedFrames\":" << audio.SubmittedFrames() << ",\"nonzeroAudioSamples\":" << audio.NonzeroSamples()
            << ",\"nativeInputEvents\":" << platform.NativeInputEvents()
            << ",\"fileDialogsCompleted\":" << platform.FileDialogsCompleted()
            << ",\"fileSelectionsCompleted\":" << platform.FileSelectionsCompleted()
            << ",\"scriptedHeadlessFilePicker\":" << (hidden && dialogTest ? "true" : "false")
            << ",\"firstCaptureHash\":\"" << graphics.firstCaptureHash.load() << "\",\"lastCaptureHash\":\"" << graphics.lastCaptureHash.load() << "\""
            << ",\"audibleAudioVerified\":false,\"result\":" << result << "}\n";
    if (benchmark)
    {
        if (samples.size() != measure || platform.NativeInputEvents() != 0)
        {
            result = 1;
        }
        LitePlayground::WriteFrameReceipt(benchmarkOutput, warmup, measure, retainedUi, samples, uiBefore, uiAfter, result);
    }
    DestroyWindow(window);
    Diagnostics::SetExitCode(result);
    return result;
}
