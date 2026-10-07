#include "StandardBinding.h"
#include "../../Source/Platform.h"
#include "../../../../LitePlayground/Source/NativeAudio.h"
#include <babylon_lite_shader_compiler.h>
#include <bgfx/bgfx.h>
#include <objbase.h>
#include <malloc.h>
#include <atomic>
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
        bool validation{};
        std::filesystem::path receipt;
        std::filesystem::path capture;
    };

    Configuration s_config;
    bl_Runtime* s_runtime{};
    bl_AudioService s_audio{};
    LitePlayground::NativeAudio* s_nativeAudio{};
    std::vector<bl_AudioEngine> s_audioEngines;
    HWND s_window{};
    bool s_initialized{};
    std::exception_ptr s_platformError;
    LARGE_INTEGER s_frequency{};
    int64_t s_start{};

    int64_t Counter()
    {
        LARGE_INTEGER value{};
        QueryPerformanceCounter(&value);
        return value.QuadPart;
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
            std::fprintf(stderr, "No-UI native GPU fatal: %s\n", text);
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
                    distinct += std::memcmp(pixel, pixels, 3) != 0;
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

        static bl_Status Wait(void* user)
        {
            auto& graphics = *static_cast<Graphics*>(user);
            bgfx::TextureRegion region{};
            region.handle = graphics.fence;
            const auto ready = bgfx::read(region, graphics.fenceBytes.data());
            for (uint32_t index = 0; index < 32; ++index)
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

    template<class T> void Array(std::ofstream& stream, const std::vector<T>& values)
    {
        stream << '[';
        for (size_t index = 0; index < values.size(); ++index)
        {
            stream << (index ? "," : "") << values[index];
        }
        stream << ']';
    }
}

namespace LiteMinecraft
{
    bl_Runtime* Runtime()
    {
        return s_runtime;
    }

    const bl_AudioService& AudioService()
    {
        return s_audio;
    }

    void RegisterAudioEngine(bl_AudioEngine engine)
    {
        s_audioEngines.push_back(engine);
    }

    void SetWorldProbe(std::function<WorldSnapshot()>)
    {
        throw std::runtime_error("Canonical timed user code must not install a world observer.");
    }

    bl_NativeEngineOptions NativeOptions()
    {
        bl_NativeEngineOptions options{};
        options.ownership = BL_BGFX_BORROWED;
        options.backend = BL_RENDERER_D3D11;
        options.platform.nativeWindowHandle = s_window;
        options.target = {BL_TARGET_SWAPCHAIN, BL_INVALID_BGFX_HANDLE, 1280, 720, BL_COLOR_BGRA8,
                          BL_DEPTH_D24S8,      s_config.samples,       0,    32};
        options.hostUserData = &s_graphics;
        options.isExternalBgfxInitialized = [](void*) { return s_initialized; };
        options.waitForSubmittedWork = Graphics::Wait;
        return options;
    }

    void Run(bbl::Engine& engine)
    {
        constexpr double deltaMs = 1000.0 / 60.0;
        const auto limit =
            s_config.benchmark ? s_config.warmup + s_config.measured : s_config.frames;
        std::vector<double> coreSamples;
        std::vector<double> totalSamples;
        std::vector<double> gpuSamples;
        std::vector<uint32_t> gpuFrames;
        std::vector<uint64_t> draws;
        coreSamples.reserve(s_config.measured);
        totalSamples.reserve(s_config.measured);
        gpuSamples.reserve(s_config.measured);
        gpuFrames.reserve(s_config.measured);
        draws.reserve(s_config.measured);
        const double startupMs = Milliseconds(s_start, Counter());
        bbl::Check(bl_startEngine(engine.state->engine, nullptr, nullptr));
        uint32_t rendered{};
        int64_t previousFrame = Counter();
        bl_EngineStats stats{};
        while ((!limit || rendered < limit) && !PlatformClosed())
        {
            Pump();
            if (s_config.replay)
            {
                Replay(rendered + 1);
            }
            DispatchPlatformEvents();
            const auto begin = Counter();
            const double frameDelta =
                limit ? deltaMs : std::min(100.0, Milliseconds(previousFrame, begin));
            previousFrame = begin;
            TickPlatform(frameDelta);
            for (const auto audio : s_audioEngines)
            {
                const auto status = bl_pollAudioEngine(audio, Milliseconds(s_start, begin));
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
            const auto frame = bgfx::frame();
            const auto coreEnd = Counter();
            bbl::js::collect_at_frame_boundary();
            const auto end = Counter();
            bbl::Check(bl_getEngineStats(engine.state->engine, &stats));
            if (s_config.benchmark && rendered >= s_config.warmup)
            {
                coreSamples.push_back(Milliseconds(coreBegin, coreEnd));
                totalSamples.push_back(Milliseconds(begin, end));
                draws.push_back(stats.drawCallCount);
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
            }
            if (!limit)
            {
                Sleep(1);
            }
        }
        bbl::Check(bl_stopEngine(engine.state->engine));
        NoUi::RequireNoHud();
        if (limit && rendered != limit)
        {
            throw std::runtime_error("Interrupted bounded canonical no-UI run.");
        }
        if (!s_config.capture.empty())
        {
            bbl::Check(bl_waitForGpuIdle(engine.state->engine));
            for (uint32_t frame = 0; frame < 4; ++frame)
            {
                bgfx::frame();
            }
            if (s_graphics.captureFailed || s_graphics.captures < 2)
            {
                throw std::runtime_error("Canonical no-UI GPU capture failed.");
            }
        }
        const auto observation = NoUi::Observe(engine);
        if (!s_config.receipt.empty())
        {
            if (!s_config.receipt.parent_path().empty())
            {
                std::filesystem::create_directories(s_config.receipt.parent_path());
            }
            std::ofstream stream(s_config.receipt, std::ios::trunc);
            stream
                << std::setprecision(17)
                << "{\"frames\":" << (s_config.benchmark ? s_config.measured : rendered)
                << ",\"warmupFrames\":" << (s_config.benchmark ? s_config.warmup : 0)
                << ",\"seed\":1337,\"radius\":6,\"width\":1280,\"height\":720,"
                << "\"configurationSource\":\"canonical staged TypeScript, not private world observation\","
                << "\"sampleCount\":" << static_cast<uint32_t>(s_config.samples)
                << ",\"fixedDeltaMs\":" << deltaMs
                << ",\"backend\":\"bgfx D3D11\",\"noUi\":true,\"vsync\":false,"
                << "\"hudDraws\":0,\"uiVertices\":0,\"hudBackingRenders\":" << HudBackingRenders()
                << ',' << "\"privateWorldObserved\":false,\"worldHash\":null,\"chunks\":null,"
                << "\"materialCount\":" << observation.materialCount
                << ",\"sceneCount\":" << observation.sceneCount << ",\"cameraPosition\":["
                << observation.cameraPosition.x << ',' << observation.cameraPosition.y << ','
                << observation.cameraPosition.z << ']' << ",\"cameraTarget\":["
                << observation.cameraTarget.x << ',' << observation.cameraTarget.y << ','
                << observation.cameraTarget.z << ']' << ",\"validationOnlyGeometryObservation\":"
                << (s_config.validation ? "true" : "false") << ",\"geometryFactories\":";
            if (s_config.validation)
            {
                stream << observation.geometryFactories << ",\"geometryInputHash\":\""
                       << observation.geometryDigest << "\""
                       << ",\"vertices\":" << observation.vertices
                       << ",\"indices\":" << observation.indices
                       << ",\"liveMeshes\":" << observation.liveMeshes;
            }
            else
            {
                stream << "null,\"geometryInputHash\":null,\"vertices\":null,\"indices\":null,"
                          "\"liveMeshes\":null";
            }
            stream << ",\"membershipEvents\":";
            if (s_config.validation)
            {
                stream << observation.membershipEvents << ",\"removals\":" << observation.removals;
            }
            else
            {
                stream << "null,\"removals\":null";
            }
            stream
                << ",\"draws\":" << stats.drawCallCount << ",\"inputEvents\":" << InputEvents()
                << ",\"captures\":" << s_graphics.captures.load() << ",\"captureHash\":\""
                << s_graphics.captureHash.load() << "\""
                << ",\"startupMs\":" << startupMs << ",\"audioOutputAvailable\":"
                << (s_nativeAudio->OutputAvailable() ? "true" : "false")
                << ",\"audioNonzeroSamples\":" << s_nativeAudio->NonzeroSamples()
                << ",\"measurementScope\":\"QPC complete no-UI native host frame: timers, "
                   "audio polling, canonical user update, C99 rendering, bgfx frame submission/"
                   "present, user-language GC. Excludes OS poll/replay, captures/readback/PNG, "
                   "receipt I/O, sleeps. No GDI backing or RmlUI layout/record/composition.\","
                << "\"coreMeasurementScope\":\"bl_frame canonical update/render plus bgfx::frame; "
                   "excludes GC and platform timers/audio polling\","
                << "\"color\":\"BGRA8 swapchain; original nearest sRGB RGBA atlas\","
                << "\"depth\":\"left-handed reversed-Z greater-equal D24S8\","
                << "\"gpuProfilingEnabled\":true,\"cpuSamplesMs\":";
            Array(stream, totalSamples);
            stream << ",\"totalHostCpuSamplesMs\":";
            Array(stream, totalSamples);
            stream << ",\"coreCpuSamplesMs\":";
            Array(stream, coreSamples);
            stream << ",\"gpuSamplesMs\":";
            Array(stream, gpuSamples);
            stream << ",\"gpuTimerUnavailableSentinel\":-1,"
                      "\"gpuMeasurementScope\":\"Real asynchronous bgfx GPU timestamp-query "
                      "intervals; GPU frame IDs may repeat and must be deduplicated. "
                      "Unavailable queries are -1, not measured zeros.\",\"gpuFrames\":";
            Array(stream, gpuFrames);
            stream << ",\"drawSamples\":";
            Array(stream, draws);
            stream << "}\n";
            if (!stream)
            {
                throw std::runtime_error("Cannot write canonical no-UI receipt.");
            }
        }
        for (const auto& callback : engine.state->callbacks)
        {
            bbl::Check(bl_removeSceneCallback(callback.first, callback.second));
        }
        engine.state->callbacks.clear();
        engine.state->beforeRender.clear();
        std::printf("Canonical no-UI C99 game: frames=%u draws=%llu input=%llu\n", rendered,
                    static_cast<unsigned long long>(stats.drawCallCount),
                    static_cast<unsigned long long>(InputEvents()));
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
            if (option == "--headless")
            {
                s_config.hidden = true;
            }
            else if (option == "--replay")
            {
                s_config.replay = true;
            }
            else if (option == "--validate-bindings")
            {
                s_config.validation = true;
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
                throw std::runtime_error("Unknown canonical no-UI option: " + option);
            }
        }
        if (s_config.benchmark && (s_config.validation || !s_config.capture.empty()))
        {
            throw std::runtime_error("Validation hashing/captures are excluded from timed runs.");
        }
        if (s_config.samples != 1 && s_config.samples != 4)
        {
            throw std::runtime_error("Experimental native host supports sample counts 1 or 4.");
        }
        if (!s_config.receipt.empty())
        {
            std::filesystem::remove(s_config.receipt);
        }
        NoUi::EnableValidation(s_config.validation);
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = WindowMessage;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = L"LiteMinecraftCanonicalNoUi";
        RegisterClassW(&windowClass);
        constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        RECT bounds{0, 0, 1280, 720};
        AdjustWindowRect(&bounds, style, FALSE);
        s_window = CreateWindowW(windowClass.lpszClassName, L"Canonical no-UI Minecraft / C99",
                                 style, CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left,
                                 bounds.bottom - bounds.top, nullptr, nullptr,
                                 windowClass.hInstance, nullptr);
        if (!s_window)
        {
            throw std::runtime_error("Cannot create canonical no-UI window.");
        }
        LiteMinecraft::InitializePlatform(s_window, !s_config.frames && !s_config.benchmark &&
                                                        !s_config.hidden);
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
            throw std::runtime_error("Canonical no-UI D3D11 initialization failed.");
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
        { return _aligned_malloc(bytes, std::max(alignment, sizeof(void*))); };
        options.allocator.deallocate = [](void*, void* memory, size_t, size_t)
        { _aligned_free(memory); };
        options.shaderCompiler = &compiler;
        options.audio = &s_audio;
        bbl::Check(bl_createRuntime(&options, &s_runtime));
        const int result = MinecraftApplication();
        LiteMinecraft::DisposePlatform();
        LiteMinecraft::ReleaseEngines();
        bbl::js::collect_cycles();
        bbl::Check(bl_disposeRuntime(s_runtime));
        s_runtime = nullptr;
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
