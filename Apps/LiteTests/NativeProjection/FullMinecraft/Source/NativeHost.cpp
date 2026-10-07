#include "Platform.h"
#include "../../../LitePlayground/Source/NativeAudio.h"
#include <babylon_lite_shader_compiler.h>
#include <bgfx/bgfx.h>
#include <objbase.h>
#include <malloc.h>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>

int MinecraftApplication();

namespace
{
    struct Configuration
    {
        uint32_t frames{};
        uint32_t warmup{180};
        uint32_t measured{1200};
        uint8_t samples{1};
        bool benchmark{};
        bool hidden{};
        bool replay{};
        bool saveLoadTest{};
        bool adapterContract{};
        std::filesystem::path receipt;
        std::filesystem::path capture;
    };

    Configuration s_config;
    bl_Runtime* s_runtime{};
    bl_AudioService s_audio{};
    HWND s_window{};
    bool s_initialized{};
    LARGE_INTEGER s_frequency{};
    int64_t s_start{};
    std::exception_ptr s_platformError;
    std::function<LiteMinecraft::WorldSnapshot()> s_worldProbe;
    LitePlayground::NativeAudio* s_nativeAudio{};
    std::vector<bl_AudioEngine> s_audioEngines;

    int64_t Counter()
    {
        LARGE_INTEGER counter{};
        QueryPerformanceCounter(&counter);
        return counter.QuadPart;
    }

    double Milliseconds(int64_t begin, int64_t end)
    {
        return static_cast<double>(end - begin) * 1000.0 /
               static_cast<double>(s_frequency.QuadPart);
    }

    uint32_t EnvironmentNumber(const char* name, uint32_t fallback)
    {
        char value[64]{};
        const auto length = GetEnvironmentVariableA(name, value, sizeof(value));
        return length && length < sizeof(value) ? static_cast<uint32_t>(std::stoul(value))
                                                : fallback;
    }

    struct Graphics : bgfx::CallbackI
    {
        std::atomic<uint32_t> captures{};
        std::atomic<bool> captureFailed{};
        std::atomic<uint64_t> captureHash{};
        bgfx::TextureHandle fence{BGFX_INVALID_HANDLE};
        std::array<uint8_t, 4> fenceBytes{};

        void fatal(const char*, uint16_t, bgfx::Fatal::Enum, const char* text) override
        {
            std::fprintf(stderr, "Native GPU fatal: %s\n", text);
            std::_Exit(3);
        }

        void traceVargs(const char*, uint16_t, const char* format, va_list arguments) override
        {
            char text[2048]{};
            vsnprintf(text, sizeof(text), format, arguments);
            OutputDebugStringA(text);
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

        void screenShot(const char* path, uint32_t width, uint32_t height, uint32_t pitch,
                        bgfx::TextureFormat::Enum format, const void* data, uint32_t,
                        bool flip) override
        {
            if (!data || format != bgfx::TextureFormat::BGRA8)
            {
                captureFailed = true;
                return;
            }
            const auto* pixels = static_cast<const uint8_t*>(data);
            uint64_t hash = 1469598103934665603ull;
            size_t distinct{};
            for (uint32_t y = 0; y < height; ++y)
            {
                for (uint32_t x = 0; x < width; ++x)
                {
                    const auto* pixel = pixels + static_cast<size_t>(y) * pitch + x * 4;
                    if (std::memcmp(pixel, pixels, 3) != 0)
                    {
                        ++distinct;
                    }
                    for (uint32_t channel = 0; channel < 3; ++channel)
                    {
                        hash = (hash ^ pixel[channel]) * 1099511628211ull;
                    }
                }
            }
            captureHash = hash;
            if (distinct < static_cast<size_t>(width) * height / 10 ||
                !LiteMinecraft::WritePNG(path, width, height, pitch, pixels, flip))
            {
                captureFailed = true;
            }
            ++captures;
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

        static bl_Status Wait(void* userData)
        {
            auto& graphics = *static_cast<Graphics*>(userData);
            bgfx::TextureRegion region{};
            region.handle = graphics.fence;
            const uint32_t ready = bgfx::read(region, graphics.fenceBytes.data());
            for (uint32_t frame = 0; frame < 32; ++frame)
            {
                if (bgfx::frame() >= ready)
                {
                    return BL_OK;
                }
            }
            return BL_HOST_ERROR;
        }
    };

    Graphics s_graphics;

    void Pump()
    {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (s_platformError)
        {
            std::rethrow_exception(s_platformError);
        }
    }

    LRESULT CALLBACK WindowMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        try
        {
            return LiteMinecraft::PlatformMessage(window, message, wParam, lParam);
        }
        catch (...)
        {
            s_platformError = std::current_exception();
            return DefWindowProcW(window, message, wParam, lParam);
        }
    }

    void Array(std::ofstream& stream, const std::vector<double>& values)
    {
        stream << '[';
        for (size_t index = 0; index < values.size(); ++index)
        {
            if (index)
            {
                stream << ',';
            }
            stream << values[index];
        }
        stream << ']';
    }
}

namespace LiteMinecraft
{
    void RegisterAudioEngine(bl_AudioEngine engine)
    {
        s_audioEngines.push_back(engine);
    }

    void SetWorldProbe(std::function<WorldSnapshot()> probe)
    {
        s_worldProbe = std::move(probe);
    }

    bl_Runtime* Runtime()
    {
        return s_runtime;
    }

    const bl_AudioService& AudioService()
    {
        return s_audio;
    }

    bl_NativeEngineOptions NativeOptions()
    {
        bl_NativeEngineOptions options{};
        options.ownership = BL_BGFX_BORROWED;
        options.backend = BL_RENDERER_D3D11;
        options.platform.nativeWindowHandle = s_window;
        options.target.kind = BL_TARGET_SWAPCHAIN;
        options.target.framebufferIndex = BL_INVALID_BGFX_HANDLE;
        options.target.width = 1280;
        options.target.height = 720;
        options.target.colorFormat = BL_COLOR_BGRA8;
        options.target.depthFormat = BL_DEPTH_D24S8;
        options.target.sampleCount = s_config.samples;
        options.target.viewCount = 32;
        options.hostUserData = &s_graphics;
        options.isExternalBgfxInitialized = [](void*) { return s_initialized; };
        options.waitForSubmittedWork = Graphics::Wait;
        return options;
    }

    void Run(bbl::Engine& engine)
    {
        const double deltaMs = 1000.0 / 60.0;
        const uint32_t limit =
            s_config.benchmark ? s_config.warmup + s_config.measured : s_config.frames;
        std::vector<double> coreSamples;
        std::vector<double> totalSamples;
        std::vector<double> gpuSamples;
        std::vector<uint32_t> gpuFrames;
        std::vector<uint64_t> drawSamples;
        coreSamples.reserve(s_config.measured);
        totalSamples.reserve(s_config.measured);
        gpuSamples.reserve(s_config.measured);
        gpuFrames.reserve(s_config.measured);
        drawSamples.reserve(s_config.measured);
        const double startupMs = Milliseconds(s_start, Counter());
        const auto initialWorld = s_worldProbe();
        if (initialWorld.chunks != 169)
        {
            throw std::runtime_error("Full native Minecraft active chunk count: " +
                                     std::to_string(initialWorld.chunks));
        }
        std::printf("Original native Minecraft ready: SEED 1337, radius 6, 1280x720, D3D11\n");
        bbl::Check(bl_startEngine(engine.state->engine, nullptr, nullptr));
        uint32_t rendered{};
        std::optional<WorldSnapshot> savedWorld;
        bool saveLoadVerified{};
        int64_t previousFrame = Counter();
        bl_EngineStats stats{};
        while ((!limit || rendered < limit) && !PlatformClosed())
        {
            Pump();
            if (s_config.replay)
            {
                Replay(rendered + 1);
            }
            if (s_config.saveLoadTest)
            {
                if (rendered + 1 == 185)
                {
                    savedWorld = s_worldProbe();
                }
                SaveLoadReplay(rendered + 1);
            }
            DispatchPlatformEvents();
            if (s_config.saveLoadTest && rendered + 1 == 215)
            {
                const auto loaded = s_worldProbe();
                saveLoadVerified =
                    savedWorld && loaded.hash == savedWorld->hash &&
                    loaded.seed == savedWorld->seed && loaded.edits == savedWorld->edits &&
                    std::abs(loaded.timeOfDay - savedWorld->timeOfDay) < 1e-9 &&
                    std::abs(loaded.playerPosition.x - savedWorld->playerPosition.x) < 1e-9 &&
                    std::abs(loaded.playerPosition.y - savedWorld->playerPosition.y) < 1e-9 &&
                    std::abs(loaded.playerPosition.z - savedWorld->playerPosition.z) < 1e-9;
            }
            const auto totalBegin = Counter();
            const double frameDelta =
                !limit ? std::min(100.0, Milliseconds(previousFrame, totalBegin)) : deltaMs;
            previousFrame = totalBegin;
            TickPlatform(frameDelta);
            const double monotonicMs = Milliseconds(s_start, Counter());
            for (const auto audio : s_audioEngines)
            {
                const auto status = bl_pollAudioEngine(audio, monotonicMs);
                if (status != BL_AUDIO_SUSPENDED && status != BL_AUDIO_UNAVAILABLE)
                {
                    bbl::Check(status);
                }
            }
            const auto coreBegin = Counter();
            bbl::Check(bl_frame(engine.state->engine, frameDelta));
            if (engine.state->callbackError)
            {
                std::rethrow_exception(engine.state->callbackError);
            }
            const uint32_t frame = bgfx::frame();
            const auto coreEnd = Counter();
            PresentHUD();
            bbl::js::collect_at_frame_boundary();
            const auto totalEnd = Counter();
            bbl::Check(bl_getEngineStats(engine.state->engine, &stats));
            if (s_config.benchmark && rendered >= s_config.warmup)
            {
                coreSamples.push_back(Milliseconds(coreBegin, coreEnd));
                totalSamples.push_back(Milliseconds(totalBegin, totalEnd));
                drawSamples.push_back(stats.drawCallCount);
                const auto* gpu = bgfx::getStats();
                const bool available = gpu->gpuTimerFreq > 0 &&
                                       gpu->gpuTimeEnd > gpu->gpuTimeBegin &&
                                       gpu->gpuFrameNum <= frame;
                gpuSamples.push_back(
                    available ? static_cast<double>(gpu->gpuTimeEnd - gpu->gpuTimeBegin) * 1000.0 /
                                    static_cast<double>(gpu->gpuTimerFreq)
                              : -1);
                gpuFrames.push_back(gpu->gpuFrameNum);
            }
            ++rendered;
            if (!s_config.capture.empty() && (rendered == 1 || rendered == limit))
            {
                const auto path = s_config.capture / ("frame" + std::to_string(rendered) + ".png");
                bgfx::requestScreenShot(BGFX_INVALID_HANDLE, path.string().c_str());
                CaptureHUD(s_config.capture / ("hud" + std::to_string(rendered) + ".png"));
            }
            if (!s_config.benchmark && !s_config.frames)
            {
                Sleep(1);
            }
        }
        bbl::Check(bl_stopEngine(engine.state->engine));
        if (limit && rendered != limit)
        {
            throw std::runtime_error("Native bounded run was interrupted.");
        }
        if (s_config.benchmark && totalSamples.size() != s_config.measured)
        {
            throw std::runtime_error("Native benchmark did not collect the requested samples.");
        }
        if (s_config.saveLoadTest && (FileDialogs() != 2 || !saveLoadVerified))
        {
            throw std::runtime_error(
                "Native save/load verification failed: dialogs=" + std::to_string(FileDialogs()) +
                " restored=" + std::to_string(saveLoadVerified));
        }
        if (!s_config.capture.empty())
        {
            bbl::Check(bl_waitForGpuIdle(engine.state->engine));
            for (uint32_t index = 0; index < 4; ++index)
            {
                bgfx::frame();
            }
            if (s_graphics.captureFailed || s_graphics.captures < 2)
            {
                throw std::runtime_error("Native Minecraft GPU capture validation failed.");
            }
        }
        if (!s_config.receipt.empty())
        {
            const auto world = s_worldProbe();
            const auto parent = s_config.receipt.parent_path();
            if (!parent.empty())
            {
                std::filesystem::create_directories(parent);
            }
            std::ofstream stream(s_config.receipt, std::ios::trunc);
            stream << std::setprecision(17);
            stream << "{\n\"frames\":" << (s_config.benchmark ? s_config.measured : rendered)
                   << ",\"warmupFrames\":" << (s_config.benchmark ? s_config.warmup : 0)
                   << ",\"seed\":1337,\"radius\":6,\"width\":1280,\"height\":720,"
                   << "\"sampleCount\":" << static_cast<uint32_t>(s_config.samples)
                   << ",\"fixedDeltaMs\":" << deltaMs
                   << ",\"backend\":\"bgfx D3D11\",\"vsync\":false,\"hidden\":"
                   << (s_config.hidden ? "true" : "false")
                   << ",\"shaderCompilation\":\"original WGSL, full Tint/SPIRV-Cross/FXC before "
                      "steady state\",\"depth\":\"left-handed reversed-Z greater-equal D24S8\","
                   << "\"color\":\"BGRA8 swapchain, original sRGB nearest-filter atlas\","
                   << "\"measurementScope\":\"QPC total host frame: platform timers, original "
                      "application update, C99 transforms/materials/render, bgfx frame submission/"
                      "present, native HUD GDI backing render, user-language cycle collection. "
                      "Excludes OS poll/input replay, capture, JSON I/O, sleep, vsync. Hidden HUD "
                      "has no desktop overlay presentation; native HUD differs from RmlUI.\","
                   << "\"coreMeasurementScope\":\"QPC bl_frame including original update and "
                      "native rendering plus bgfx::frame submission/present; excludes HUD and GC\","
                   << "\"startupMs\":" << startupMs << ",\"inputEvents\":" << InputEvents()
                   << ",\"draws\":" << stats.drawCallCount
                   << ",\"captures\":" << s_graphics.captures.load() << ",\"captureHash\":\""
                   << s_graphics.captureHash.load() << "\""
                   << ",\"chunks\":" << world.chunks << ",\"nonzeroBlocks\":" << world.nonzeroBlocks
                   << ",\"worldHash\":\"" << world.hash << "\""
                   << ",\"initialWorldHash\":\"" << initialWorld.hash << "\""
                   << ",\"mobs\":" << world.mobs
                   << ",\"materialCount\":" << engine.state->materialBindings.size()
                   << ",\"worldSeed\":" << world.seed << ",\"blockEdits\":" << world.edits
                   << ",\"fileDialogs\":" << FileDialogs()
                   << ",\"saveLoadVerified\":" << (saveLoadVerified ? "true" : "false")
                   << ",\"audioOutputAvailable\":"
                   << (s_nativeAudio->OutputAvailable() ? "true" : "false")
                   << ",\"audioSubmittedFrames\":" << s_nativeAudio->SubmittedFrames()
                   << ",\"audioNonzeroSamples\":" << s_nativeAudio->NonzeroSamples()
                   << ",\"cameraPosition\":[" << world.cameraPosition.x << ','
                   << world.cameraPosition.y << ',' << world.cameraPosition.z << ']'
                   << ",\"cameraTarget\":[" << world.cameraTarget.x << ',' << world.cameraTarget.y
                   << ',' << world.cameraTarget.z << ']' << ",\"timeOfDay\":" << world.timeOfDay
                   << ",\"cpuSamplesMs\":";
            Array(stream, totalSamples);
            stream << ",\"totalHostCpuSamplesMs\":";
            Array(stream, totalSamples);
            stream << ",\"coreCpuSamplesMs\":";
            Array(stream, coreSamples);
            stream << ",\"gpuSamplesMs\":";
            Array(stream, gpuSamples);
            stream
                << ",\"gpuTimerUnavailableSentinel\":-1,"
                   "\"gpuMeasurementScope\":\"Asynchronous real bgfx GPU timestamp-query interval; "
                   "GPU frame IDs are reported separately, may repeat, and are not CPU frame IDs. "
                   "Unavailable query pairs are -1, never a measured zero.\",\"gpuFrames\":[";
            for (size_t index = 0; index < gpuFrames.size(); ++index)
            {
                stream << (index ? "," : "") << gpuFrames[index];
            }
            stream << "],\"drawSamples\":[";
            for (size_t index = 0; index < drawSamples.size(); ++index)
            {
                stream << (index ? "," : "") << drawSamples[index];
            }
            stream << "]\n}\n";
            if (!stream)
            {
                throw std::runtime_error("Native benchmark receipt write failed.");
            }
        }
        std::printf("Native Minecraft finished: frames=%u draws=%llu input=%llu\n", rendered,
                    static_cast<unsigned long long>(stats.drawCallCount),
                    static_cast<unsigned long long>(InputEvents()));
        for (const auto& callback : engine.state->callbacks)
        {
            bbl::Check(bl_removeSceneCallback(callback.first, callback.second));
        }
        engine.state->callbacks.clear();
        engine.state->beforeRender.clear();
        s_worldProbe = {};
    }
}

int main(int argc, char** argv)
{
    QueryPerformanceFrequency(&s_frequency);
    s_start = Counter();
    try
    {
        s_config.warmup = EnvironmentNumber("BBLITE_BENCHMARK_WARMUP_FRAMES", 180);
        s_config.measured = EnvironmentNumber("BBLITE_BENCHMARK_FRAMES", 1200);
        s_config.samples = static_cast<uint8_t>(EnvironmentNumber("BBLITE_MSAA", 1));
        for (int index = 1; index < argc; ++index)
        {
            const std::string option(argv[index]);
            if (option == "--benchmark")
            {
                s_config.benchmark = true;
                s_config.hidden = true;
            }
            else if (option == "--headless")
            {
                s_config.hidden = true;
            }
            else if (option == "--replay")
            {
                s_config.replay = true;
            }
            else if (option == "--test-save-load")
            {
                s_config.saveLoadTest = true;
                s_config.replay = true;
            }
            else if (option == "--adapter-contract")
            {
                s_config.adapterContract = true;
                s_config.hidden = true;
            }
            else if (option.starts_with("--frames="))
            {
                s_config.frames = static_cast<uint32_t>(std::stoul(option.substr(9)));
            }
            else if (option.starts_with("--warmup="))
            {
                s_config.warmup = static_cast<uint32_t>(std::stoul(option.substr(9)));
            }
            else if (option.starts_with("--measure="))
            {
                s_config.measured = static_cast<uint32_t>(std::stoul(option.substr(10)));
            }
            else if (option.starts_with("--benchmark-output="))
            {
                s_config.receipt = option.substr(19);
                s_config.benchmark = true;
                s_config.hidden = true;
            }
            else if (option.starts_with("--summary="))
            {
                s_config.receipt = option.substr(10);
            }
            else if (option.starts_with("--capture-root="))
            {
                s_config.capture = option.substr(15);
            }
            else
            {
                throw std::runtime_error("Unknown native Minecraft option: " + option);
            }
        }
        if (s_config.benchmark && !s_config.capture.empty())
        {
            throw std::runtime_error("Benchmark forbids capture/readback.");
        }
        if (s_config.benchmark && (s_config.saveLoadTest || s_config.adapterContract))
        {
            throw std::runtime_error("Benchmark forbids file-dialog and adapter-contract modes.");
        }
        if (s_config.samples != 1 && s_config.samples != 4)
        {
            throw std::runtime_error("This native host currently accepts MSAA 1 or 4 only.");
        }
        if (!s_config.receipt.empty())
        {
            std::filesystem::remove(s_config.receipt);
        }
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = WindowMessage;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = L"LiteMinecraftPureNative";
        windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        RegisterClassW(&windowClass);
        constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        RECT rectangle{0, 0, 1280, 720};
        AdjustWindowRect(&rectangle, style, FALSE);
        s_window = CreateWindowW(windowClass.lpszClassName,
                                 L"Babylon Lite — C99 full native Minecraft — SEED 1337 / radius 6",
                                 style, CW_USEDEFAULT, CW_USEDEFAULT,
                                 rectangle.right - rectangle.left, rectangle.bottom - rectangle.top,
                                 nullptr, nullptr, windowClass.hInstance, nullptr);
        if (!s_window)
        {
            throw std::runtime_error("Native Minecraft window creation failed.");
        }
        LiteMinecraft::InitializePlatform(s_window, !s_config.frames && !s_config.benchmark &&
                                                        !s_config.hidden &&
                                                        !s_config.adapterContract);
        if (s_config.saveLoadTest)
        {
            LiteMinecraft::ConfigureSaveLoadTest(
                s_config.capture / ("world-" + std::to_string(GetCurrentProcessId()) + ".json"));
        }
        if (!s_config.hidden)
        {
            ShowWindow(s_window, SW_SHOW);
        }
        bgfx::Init init{};
        init.type = bgfx::RendererType::Direct3D11;
        init.fallback = false;
        init.swapChain.nwh = s_window;
        init.swapChain.width = 1280;
        init.swapChain.height = 720;
        init.swapChain.flags = s_config.samples == 4 ? BGFX_SWAP_CHAIN_MSAA_X4 : 0;
        init.swapChain.formatColor = bgfx::TextureFormat::BGRA8;
        init.swapChain.formatDepthStencil = bgfx::TextureFormat::D24S8;
        init.profile = true;
        init.callback = &s_graphics;
        if (!bgfx::init(init))
        {
            throw std::runtime_error("Native D3D11 initialization failed.");
        }
        s_initialized = true;
        s_graphics.fence = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8,
                                                 BGFX_TEXTURE_READ_BACK | BGFX_SAMPLER_POINT);
        LitePlayground::NativeAudio audio;
        s_nativeAudio = &audio;
        s_audio = audio.Service();
        const auto compiler = bl_shaderCompilerService();
        bl_RuntimeOptions options{};
        options.allocator.allocate = [](void*, size_t bytes, size_t alignment)
        { return _aligned_malloc(bytes, alignment); };
        options.allocator.deallocate = [](void*, void* memory, size_t, size_t)
        { _aligned_free(memory); };
        options.shaderCompiler = &compiler;
        options.audio = &s_audio;
        bbl::Check(bl_createRuntime(&options, &s_runtime));
        int result{};
        if (s_config.adapterContract)
        {
            LiteMinecraft::RunAdapterContract();
        }
        else
        {
            result = MinecraftApplication();
        }
        LiteMinecraft::DisposePlatform();
        LiteMinecraft::ReleaseEngines();
        bbl::js::collect_cycles();
        bbl::Check(bl_disposeRuntime(s_runtime));
        s_runtime = nullptr;
        s_audioEngines.clear();
        bgfx::destroy(s_graphics.fence);
        bgfx::frame();
        bgfx::shutdown();
        s_initialized = false;
        DestroyWindow(s_window);
        CoUninitialize();
        return result;
    }
    catch (...)
    {
        return bbl::report_uncaught_error(std::current_exception());
    }
}
