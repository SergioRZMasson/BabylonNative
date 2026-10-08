#include "StandardBinding.h"
#include "UiBridge.h"
#include "../../Source/Platform.h"
#include "../../../../LitePlayground/Source/NativeAudio.h"
#include <babylon_lite_shader_compiler.h>
#include <bgfx/bgfx.h>
#include <objbase.h>
#include <malloc.h>
#include <atomic>
#include <cstdarg>
#include <cmath>
#include <climits>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <psapi.h>
#include <windowsx.h>

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
        bool saveLoad{};
        uint32_t resizeFrame{};
        double density{1};
        bool boundaryTest{};
        bool underwaterUiTest{};
        std::string requiredEffect;
        std::filesystem::path receipt;
        std::filesystem::path capture;
    };

    Configuration s_config;
    bl_Runtime* s_runtime{};
    bl_AudioService s_audio{};
    LitePlayground::NativeAudio* s_nativeAudio{};
    std::unique_ptr<LitePlayground::NativeAudio> s_audioOwner;
    std::vector<bl_AudioEngine> s_audioEngines;
    HWND s_window{};
    bool s_initialized{};
    std::exception_ptr s_platformError;
    LARGE_INTEGER s_frequency{};
    int64_t s_start{};
    uint32_t s_width{1280};
    uint32_t s_height{720};
    double s_density{1};
    bool s_viewportChanged{};

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
            std::fprintf(stderr, "RmlUI native GPU fatal: %s\n", text);
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
            ++captures;
            try
            {
                WriteScreenShot(path, width, height, pitch, format, data, flip);
            }
            catch (...)
            {
                captureFailed = true;
                std::fprintf(stderr, "GPU screenshot host resource/write exception.\n");
            }
        }

        void WriteScreenShot(const char* path, uint32_t width, uint32_t height, uint32_t pitch,
                             bgfx::TextureFormat::Enum format, const void* data, bool flip)
        {
            if (!data || format != bgfx::TextureFormat::BGRA8)
            {
                std::fprintf(stderr, "Unexpected UI screenshot format %u (%u x %u).\n",
                             static_cast<unsigned>(format), width, height);
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
            const auto minimum = s_config.boundaryTest || !s_config.requiredEffect.empty()
                                     ? size_t{128}
                                     : static_cast<size_t>(width) * height / 10;
            const bool written = LiteMinecraft::WritePNG(path, width, height, pitch, pixels, flip);
            if (distinct < minimum || !written)
            {
                std::fprintf(stderr,
                             "GPU capture validation failed: distinct=%zu required=%zu "
                             "written=%u dimensions=%u x %u.\n",
                             distinct, minimum, static_cast<unsigned>(written), width, height);
                captureFailed = true;
            }
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

    bl_UiKey UiKey(WPARAM key)
    {
        switch (key)
        {
            case VK_BACK:
                return BL_UI_KEY_BACKSPACE;
            case VK_TAB:
                return BL_UI_KEY_TAB;
            case VK_RETURN:
                return BL_UI_KEY_ENTER;
            case VK_ESCAPE:
                return BL_UI_KEY_ESCAPE;
            case VK_SPACE:
                return BL_UI_KEY_SPACE;
            case VK_LEFT:
                return BL_UI_KEY_LEFT;
            case VK_RIGHT:
                return BL_UI_KEY_RIGHT;
            case VK_UP:
                return BL_UI_KEY_UP_ARROW;
            case VK_DOWN:
                return BL_UI_KEY_DOWN_ARROW;
            case VK_HOME:
                return BL_UI_KEY_HOME;
            case VK_END:
                return BL_UI_KEY_END;
            case VK_DELETE:
                return BL_UI_KEY_DELETE;
            case 'A':
                return BL_UI_KEY_A;
            case 'C':
                return BL_UI_KEY_C;
            case 'V':
                return BL_UI_KEY_V;
            case 'X':
                return BL_UI_KEY_X;
            case 'Y':
                return BL_UI_KEY_Y;
            case 'Z':
                return BL_UI_KEY_Z;
            default:
                return BL_UI_KEY_UNKNOWN;
        }
    }

    LRESULT CALLBACK WindowMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        try
        {
            if (message == WM_SIZE && wParam != SIZE_MINIMIZED)
            {
                s_width = LOWORD(lParam);
                s_height = HIWORD(lParam);
                s_viewportChanged = s_width && s_height;
            }
            else if (message == WM_DPICHANGED)
            {
                s_density = static_cast<double>(HIWORD(wParam)) / 96.0;
                s_viewportChanged = true;
                const auto* bounds = reinterpret_cast<const RECT*>(lParam);
                SetWindowPos(window, nullptr, bounds->left, bounds->top,
                             bounds->right - bounds->left, bounds->bottom - bounds->top,
                             SWP_NOACTIVATE | SWP_NOZORDER);
            }
            if (!s_config.frames && !s_config.benchmark && !s_config.hidden)
            {
                bl_UiInput input{};
                bool route{};
                std::string text;
                input.modifiers = (GetKeyState(VK_SHIFT) & 0x8000 ? BL_UI_MOD_SHIFT : 0) |
                                  (GetKeyState(VK_CONTROL) & 0x8000 ? BL_UI_MOD_CONTROL : 0) |
                                  (GetKeyState(VK_MENU) & 0x8000 ? BL_UI_MOD_ALT : 0);
                if (message == WM_MOUSEMOVE)
                {
                    input.kind = BL_UI_POINTER_MOVE;
                    input.x = GET_X_LPARAM(lParam);
                    input.y = GET_Y_LPARAM(lParam);
                    route = true;
                }
                else if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ||
                         message == WM_RBUTTONDOWN || message == WM_RBUTTONUP)
                {
                    input.kind = message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN
                                     ? BL_UI_POINTER_DOWN
                                     : BL_UI_POINTER_UP;
                    input.button = message == WM_RBUTTONDOWN || message == WM_RBUTTONUP ? 1 : 0;
                    input.x = GET_X_LPARAM(lParam);
                    input.y = GET_Y_LPARAM(lParam);
                    route = true;
                }
                else if (message == WM_MOUSEWHEEL)
                {
                    input.kind = BL_UI_WHEEL;
                    input.y = -static_cast<double>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;
                    route = true;
                }
                else if (message == WM_KEYDOWN || message == WM_KEYUP)
                {
                    input.kind = message == WM_KEYDOWN ? BL_UI_KEY_DOWN : BL_UI_KEY_UP;
                    input.key = UiKey(wParam);
                    route = true;
                }
                else if (message == WM_CHAR && wParam >= 32)
                {
                    static wchar_t highSurrogate{};
                    const auto character = static_cast<wchar_t>(wParam);
                    if (character >= 0xd800 && character <= 0xdbff)
                    {
                        highSurrogate = character;
                    }
                    else
                    {
                        wchar_t units[2]{highSurrogate, character};
                        const auto* source = highSurrogate ? units : &character;
                        const int count = highSurrogate ? 2 : 1;
                        char bytes[8]{};
                        const int size =
                            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source, count, bytes,
                                                sizeof(bytes), nullptr, nullptr);
                        highSurrogate = 0;
                        if (!size)
                        {
                            throw std::runtime_error("Native UI text is invalid UTF-16.");
                        }
                        text.assign(bytes, static_cast<size_t>(size));
                        input.kind = BL_UI_TEXT;
                        input.text = bbl::String(text);
                        route = true;
                    }
                }
                if (route)
                {
                    MinecraftUi::Input(input);
                }
            }
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
        options.target = {
            BL_TARGET_SWAPCHAIN, BL_INVALID_BGFX_HANDLE, s_width, s_height, BL_COLOR_BGRA8,
            BL_DEPTH_D24S8,      s_config.samples,       0,       32};
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
        std::vector<double> uiUpdateSamples;
        std::vector<double> uiRenderSamples;
        std::vector<double> presenterSamples;
        std::vector<uint64_t> uiDrawSamples;
        std::vector<uint64_t> uiGeometrySamples;
        std::vector<uint64_t> uiTextureSamples;
        std::vector<uint64_t> uiUploadSamples;
        std::vector<uint64_t> gpuFrequencies;
        std::vector<double> rendererSamples;
        std::vector<double> waitRenderSamples;
        std::vector<double> waitSubmitSamples;
        std::vector<uint32_t> gpuFrames;
        std::vector<uint64_t> draws;
        coreSamples.reserve(s_config.measured);
        totalSamples.reserve(s_config.measured);
        gpuSamples.reserve(s_config.measured);
        gpuFrames.reserve(s_config.measured);
        draws.reserve(s_config.measured);
        uiUpdateSamples.reserve(s_config.measured);
        uiRenderSamples.reserve(s_config.measured);
        presenterSamples.reserve(s_config.measured);
        uiDrawSamples.reserve(s_config.measured);
        uiGeometrySamples.reserve(s_config.measured);
        uiTextureSamples.reserve(s_config.measured);
        uiUploadSamples.reserve(s_config.measured);
        gpuFrequencies.reserve(s_config.measured);
        rendererSamples.reserve(s_config.measured);
        waitRenderSamples.reserve(s_config.measured);
        waitSubmitSamples.reserve(s_config.measured);
        uint64_t processBegin{};
        uint64_t processEnd{};
        uint64_t mainBegin{};
        uint64_t mainEnd{};
        SIZE_T privateBegin{};
        SIZE_T privateEnd{};
        const auto execution = [](bool thread)
        {
            FILETIME created{};
            FILETIME exited{};
            FILETIME kernel{};
            FILETIME user{};
            const bool success =
                thread
                    ? GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user) != FALSE
                    : GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) !=
                          FALSE;
            if (!success)
            {
                throw std::runtime_error("Cannot observe native process/thread execution.");
            }
            ULARGE_INTEGER first{};
            first.LowPart = kernel.dwLowDateTime;
            first.HighPart = kernel.dwHighDateTime;
            ULARGE_INTEGER second{};
            second.LowPart = user.dwLowDateTime;
            second.HighPart = user.dwHighDateTime;
            return first.QuadPart + second.QuadPart;
        };
        const auto privateBytes = []()
        {
            PROCESS_MEMORY_COUNTERS_EX memory{};
            if (!GetProcessMemoryInfo(GetCurrentProcess(),
                                      reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                                      sizeof(memory)))
            {
                throw std::runtime_error("Cannot observe native process memory.");
            }
            return memory.PrivateUsage;
        };
        const double startupMs = Milliseconds(s_start, Counter());
        bbl::Check(bl_startEngine(engine.state->engine, nullptr, nullptr));
        uint32_t rendered{};
        int64_t previousFrame = Counter();
        bl_EngineStats stats{};
        while ((!limit || rendered < limit) && !PlatformClosed())
        {
            Pump();
            if (s_config.resizeFrame && rendered + 1 == s_config.resizeFrame)
            {
                s_width = 960;
                s_height = 540;
                s_density = s_config.density;
                s_viewportChanged = true;
            }
            if (s_viewportChanged)
            {
                bgfx::SwapChain swapChain{};
                swapChain.width = s_width;
                swapChain.height = s_height;
                swapChain.flags =
                    s_config.samples == 4 ? BGFX_SWAP_CHAIN_MSAA_X4 : BGFX_SWAP_CHAIN_NONE;
                swapChain.formatColor = bgfx::TextureFormat::BGRA8;
                swapChain.formatDepthStencil = bgfx::TextureFormat::D24S8;
                bgfx::reset(BGFX_RESET_NONE, &swapChain);
                auto target = NativeOptions().target;
                bbl::Check(bl_setNativeTarget(engine.state->engine, &target));
                MinecraftUi::Viewport(s_width, s_height, s_density);
                s_viewportChanged = false;
            }
            if (s_config.replay)
            {
                Replay(rendered + 1);
            }
            if (s_config.saveLoad)
            {
                SaveLoadReplay(rendered + 1);
            }
            DispatchPlatformEvents();
            if (s_config.benchmark && rendered == s_config.warmup)
            {
                processBegin = execution(false);
                mainBegin = execution(true);
                privateBegin = privateBytes();
            }
            const auto begin = Counter();
            const double frameDelta =
                limit ? deltaMs : std::min(100.0, Milliseconds(previousFrame, begin));
            previousFrame = begin;
            TickPlatform(rendered ? frameDelta : 0);
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
            const auto coreEnd = Counter();
            const auto uiBegin = Counter();
            if (s_config.underwaterUiTest && rendered >= 89)
            {
                MinecraftUi::ForceUnderwaterForUiTest();
            }
            MinecraftUi::Update(limit ? static_cast<double>(rendered) / 60.0
                                      : Milliseconds(s_start, uiBegin) / 1000.0);
            const auto uiUpdated = Counter();
            MinecraftUi::Render();
            const auto uiRendered = Counter();
            if (!s_config.capture.empty() && (rendered == 0 || rendered + 1 == limit))
            {
                const auto path =
                    s_config.capture / ("frame" + std::to_string(rendered + 1) + ".png");
                bgfx::requestScreenShot(BGFX_INVALID_HANDLE, path.string().c_str());
            }
            const auto frame = bgfx::frame();
            const auto presented = Counter();
            bbl::js::collect_at_frame_boundary();
            const auto end = Counter();
            if (s_config.benchmark && rendered + 1 == limit)
            {
                processEnd = execution(false);
                mainEnd = execution(true);
                privateEnd = privateBytes();
            }
            bbl::Check(bl_getEngineStats(engine.state->engine, &stats));
            if (s_config.benchmark && rendered >= s_config.warmup)
            {
                coreSamples.push_back(Milliseconds(coreBegin, coreEnd));
                totalSamples.push_back(Milliseconds(begin, end));
                draws.push_back(stats.drawCallCount);
                const auto ui = MinecraftUi::Stats();
                if (!ui.drawCount || !ui.liveGeometryCount)
                {
                    throw std::runtime_error("Benchmark requires real retained HUD GPU draws.");
                }
                uiUpdateSamples.push_back(Milliseconds(uiBegin, uiUpdated));
                uiRenderSamples.push_back(Milliseconds(uiUpdated, uiRendered));
                presenterSamples.push_back(Milliseconds(uiRendered, presented));
                uiDrawSamples.push_back(ui.drawCount);
                uiGeometrySamples.push_back(ui.geometryCompileCount);
                uiTextureSamples.push_back(ui.textureCreateCount);
                uiUploadSamples.push_back(ui.uploadedBytes);
                const auto* gpu = bgfx::getStats();
                const bool available = gpu->gpuTimerFreq > 0 &&
                                       gpu->gpuTimeEnd > gpu->gpuTimeBegin &&
                                       gpu->gpuFrameNum <= frame;
                gpuSamples.push_back(
                    available ? static_cast<double>(gpu->gpuTimeEnd - gpu->gpuTimeBegin) * 1000.0 /
                                    static_cast<double>(gpu->gpuTimerFreq)
                              : -1);
                gpuFrames.push_back(gpu->gpuFrameNum);
                gpuFrequencies.push_back(gpu->gpuTimerFreq);
                const double cpuScale = 1000.0 / static_cast<double>(gpu->cpuTimerFreq);
                rendererSamples.push_back(static_cast<double>(gpu->cpuTimeEnd - gpu->cpuTimeBegin) *
                                          cpuScale);
                waitRenderSamples.push_back(static_cast<double>(gpu->waitRender) * cpuScale);
                waitSubmitSamples.push_back(static_cast<double>(gpu->waitSubmit) * cpuScale);
            }
            ++rendered;
            if (!limit)
            {
                Sleep(1);
            }
        }
        bbl::Check(bl_stopEngine(engine.state->engine));
        if (HudBackingRenders())
        {
            throw std::runtime_error("RmlUI host must never render a GDI HUD backing surface.");
        }
        if (limit && rendered != limit)
        {
            throw std::runtime_error("Interrupted bounded original-UI run.");
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
                throw std::runtime_error("Original-UI combined GPU capture failed.");
            }
        }
        const auto observation = MinecraftUi::Observe(engine);
        const auto ui = MinecraftUi::Stats();
        if (!ui.drawCount || !ui.liveGeometryCount || !ui.liveTextureCount)
        {
            throw std::runtime_error("Original UI did not submit retained geometry/font/images.");
        }
        if (!s_config.capture.empty())
        {
            MinecraftUi::WriteDom(s_config.capture / "dom.txt");
        }
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
                << ",\"backend\":\"bgfx D3D11\",\"noUi\":false,\"vsync\":false,"
                << "\"uiBackend\":\"LiteLayer RmlUI bgfx\",\"host\":\"Win32 not SDL3\","
                << "\"uiEnabled\":true,\"hudDraws\":" << ui.drawCount
                << ",\"uiLiveGeometry\":" << ui.liveGeometryCount
                << ",\"uiLiveTextures\":" << ui.liveTextureCount
                << ",\"uiElements\":" << ui.liveElementCount
                << ",\"uiGeometryCompiles\":" << ui.geometryCompileCount
                << ",\"uiTextureCreates\":" << ui.textureCreateCount
                << ",\"uiUploadedBytes\":" << ui.uploadedBytes
                << ",\"uiHostMutations\":" << MinecraftUi::Mutations()
                << ",\"hudBackingRenders\":" << HudBackingRenders() << ','
                << "\"privateWorldObserved\":false,\"worldHash\":null,\"chunks\":null,"
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
                << ",\"fileDialogs\":" << FileDialogs() << ",\"uiOnlyUnderwaterTestOverride\":"
                << (s_config.underwaterUiTest ? "true" : "false")
                << ",\"processCpuMs\":" << static_cast<double>(processEnd - processBegin) / 10000.0
                << ",\"mainThreadCpuMs\":" << static_cast<double>(mainEnd - mainBegin) / 10000.0
                << ",\"processPrivateBytesBegin\":" << privateBegin
                << ",\"processPrivateBytesEnd\":" << privateEnd << ",\"finalWidth\":" << s_width
                << ",\"finalHeight\":" << s_height << ",\"finalDensity\":" << s_density
                << ",\"timerClock\":\"fixed virtual simulation for bounded runs; wall for interactive\","
                << "\"fontService\":\"installed Segoe UI 400/600 and Consolas 400/700; FreeType\","
                << "\"measurementScope\":\"QPC complete original-UI native host frame: timers, "
                   "audio polling, canonical user update, C99 rendering, RmlUI update/render, bgfx frame submission/"
                   "present, user-language GC. Excludes OS poll/replay, captures/readback/PNG, "
                   "receipt I/O, sleeps. No GDI backing.\","
                << "\"coreMeasurementScope\":\"bl_frame canonical update/render; "
                   "excludes presenter, UI, GC and platform timers/audio polling\","
                << "\"color\":\"BGRA8 swapchain; original nearest sRGB RGBA atlas\","
                << "\"depth\":\"left-handed reversed-Z greater-equal D24S8\","
                << "\"gpuProfilingEnabled\":true,\"cpuSamplesMs\":";
            Array(stream, totalSamples);
            stream << ",\"totalHostCpuSamplesMs\":";
            Array(stream, totalSamples);
            stream << ",\"coreCpuSamplesMs\":";
            Array(stream, coreSamples);
            stream << ",\"rmlUpdateSamplesMs\":";
            Array(stream, uiUpdateSamples);
            stream << ",\"rmlRenderSamplesMs\":";
            Array(stream, uiRenderSamples);
            stream << ",\"presenterSamplesMs\":";
            Array(stream, presenterSamples);
            stream << ",\"uiDrawSamples\":";
            Array(stream, uiDrawSamples);
            stream << ",\"uiGeometryCompileSamples\":";
            Array(stream, uiGeometrySamples);
            stream << ",\"uiTextureCreateSamples\":";
            Array(stream, uiTextureSamples);
            stream << ",\"uiUploadSamples\":";
            Array(stream, uiUploadSamples);
            stream << ",\"gpuSamplesMs\":";
            Array(stream, gpuSamples);
            stream << ",\"gpuTimerUnavailableSentinel\":-1,"
                      "\"gpuMeasurementScope\":\"Real asynchronous bgfx GPU timestamp-query "
                      "intervals; GPU frame IDs may repeat and must be deduplicated. "
                      "Unavailable queries are -1, not measured zeros.\",\"gpuFrames\":";
            Array(stream, gpuFrames);
            stream << ",\"gpuTimerFrequencies\":";
            Array(stream, gpuFrequencies);
            stream << ",\"rendererSubmitSamplesMs\":";
            Array(stream, rendererSamples);
            stream << ",\"bgfxWaitRenderSamplesMs\":";
            Array(stream, waitRenderSamples);
            stream << ",\"bgfxWaitSubmitSamplesMs\":";
            Array(stream, waitSubmitSamples);
            stream << ",\"drawSamples\":";
            Array(stream, draws);
            stream << "}\n";
            if (!stream)
            {
                throw std::runtime_error("Cannot write original-UI receipt.");
            }
        }
        for (const auto& callback : engine.state->callbacks)
        {
            bbl::Check(bl_removeSceneCallback(callback.first, callback.second));
        }
        engine.state->callbacks.clear();
        engine.state->beforeRender.clear();
        std::printf("Original-UI C99 game: frames=%u draws=%llu input=%llu\n", rendered,
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
            else if (option == "--save-load-test")
            {
                s_config.saveLoad = true;
            }
            else if (option == "--ui-boundary-test")
            {
                s_config.boundaryTest = true;
            }
            else if (option == "--underwater-ui-test")
            {
                s_config.underwaterUiTest = true;
            }
            else if (option.starts_with("--required-effect="))
            {
                s_config.requiredEffect = option.substr(18);
            }
            else if (option.starts_with("--resize-frame="))
            {
                s_config.resizeFrame = static_cast<uint32_t>(std::stoul(option.substr(15)));
            }
            else if (option.starts_with("--density="))
            {
                s_config.density = std::stod(option.substr(10));
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
                throw std::runtime_error("Unknown original-UI option: " + option);
            }
        }
        if (s_config.benchmark &&
            (s_config.validation || !s_config.capture.empty() || s_config.saveLoad ||
             s_config.resizeFrame || s_config.boundaryTest || s_config.underwaterUiTest ||
             !s_config.requiredEffect.empty()))
        {
            throw std::runtime_error("Validation hashing/captures are excluded from timed runs.");
        }
        if (!s_config.measured || s_config.warmup > UINT32_MAX - s_config.measured ||
            !std::isfinite(s_config.density) || s_config.density <= 0 || s_config.density > 16)
        {
            throw std::runtime_error("Invalid measurement/density configuration.");
        }
        if (s_config.samples != 1 && s_config.samples != 4)
        {
            throw std::runtime_error("Experimental native host supports sample counts 1 or 4.");
        }
        if (!s_config.receipt.empty())
        {
            if (std::filesystem::exists(s_config.receipt))
            {
                throw std::runtime_error(
                    "Receipts require a fresh output path; refusing overwrite.");
            }
        }
        MinecraftUi::EnableValidation(s_config.validation);
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = WindowMessage;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = L"LiteMinecraftOriginalRmlUi";
        RegisterClassW(&windowClass);
        constexpr DWORD style = WS_OVERLAPPEDWINDOW;
        RECT bounds{0, 0, 1280, 720};
        AdjustWindowRect(&bounds, style, FALSE);
        s_window = CreateWindowW(windowClass.lpszClassName, L"Original Minecraft / LiteLayer RmlUI",
                                 style, CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left,
                                 bounds.bottom - bounds.top, nullptr, nullptr,
                                 windowClass.hInstance, nullptr);
        if (!s_window)
        {
            throw std::runtime_error("Cannot create original-UI window.");
        }
        LiteMinecraft::InitializePlatform(s_window, !s_config.frames && !s_config.benchmark &&
                                                        !s_config.hidden);
        LiteMinecraft::SetFixedPlatformClock(s_config.frames || s_config.benchmark);
        if (s_config.saveLoad)
        {
            if (s_config.capture.empty() || s_config.frames < 240)
            {
                throw std::runtime_error("Save/load test requires capture-root and frames>=240.");
            }
            std::filesystem::create_directories(s_config.capture);
            LiteMinecraft::ConfigureSaveLoadTest(s_config.capture / "world.voxelsave.json");
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
            throw std::runtime_error("Original-UI D3D11 initialization failed.");
        }
        s_initialized = true;
        if (bgfx::getCaps()->limits.maxViews < 256)
        {
            throw std::runtime_error("Original UI requires 256-view capacity: 3D0..31, "
                                     "white crosshair32..63, layered HUD64..255.");
        }
        s_viewportChanged = false;
        s_graphics.fence = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8,
                                                 BGFX_TEXTURE_READ_BACK | BGFX_SAMPLER_POINT);
        s_audioOwner = std::make_unique<LitePlayground::NativeAudio>();
        s_nativeAudio = s_audioOwner.get();
        s_audio = s_nativeAudio->Service();
        const auto compiler = bl_shaderCompilerService();
        bl_RuntimeOptions options{};
        options.allocator.allocate = [](void*, size_t bytes, size_t alignment)
        { return _aligned_malloc(bytes, std::max(alignment, sizeof(void*))); };
        options.allocator.deallocate = [](void*, void* memory, size_t, size_t)
        { _aligned_free(memory); };
        options.shaderCompiler = &compiler;
        options.audio = &s_audio;
        options.onError = [](void*, const bl_Error* error)
        {
            if (error)
            {
                std::fprintf(stderr, "C99 error %u in %.*s: %.*s\n",
                             static_cast<unsigned>(error->status),
                             static_cast<int>(std::min<size_t>(error->operation.length, INT_MAX)),
                             error->operation.data ? error->operation.data : "",
                             static_cast<int>(std::min<size_t>(error->message.length, INT_MAX)),
                             error->message.data ? error->message.data : "");
            }
        };
        bbl::Check(bl_createRuntime(&options, &s_runtime));
        const int result =
            s_config.boundaryTest || !s_config.requiredEffect.empty()
                ? MinecraftUi::RunBoundaryTest(s_config.requiredEffect, s_config.capture)
                : MinecraftApplication();
        if (s_config.boundaryTest && !s_config.capture.empty() &&
            (s_graphics.captureFailed || s_graphics.captures != 1))
        {
            throw std::runtime_error("Retained UI boundary actual GPU capture failed: received=" +
                                     std::to_string(s_graphics.captures.load()) +
                                     " failed=" + std::to_string(s_graphics.captureFailed.load()));
        }
        LiteMinecraft::DisposePlatform();
        MinecraftUi::Dispose();
        LiteMinecraft::ReleaseEngines();
        bbl::js::collect_cycles();
        bbl::Check(bl_disposeRuntime(s_runtime));
        s_runtime = nullptr;
        bgfx::destroy(s_graphics.fence);
        bgfx::frame();
        bgfx::shutdown();
        s_initialized = false;
        s_audioOwner.reset();
        s_nativeAudio = nullptr;
        DestroyWindow(s_window);
        CoUninitialize();
        return result;
    }
    catch (...)
    {
        const auto error = std::current_exception();
        try
        {
            LiteMinecraft::DisposePlatform();
            MinecraftUi::Dispose();
            LiteMinecraft::ReleaseEngines();
            bbl::js::collect_cycles();
            if (s_runtime)
            {
                bbl::Check(bl_disposeRuntime(s_runtime));
                s_runtime = nullptr;
            }
            if (s_initialized)
            {
                bgfx::destroy(s_graphics.fence);
                bgfx::frame();
                bgfx::shutdown();
                s_initialized = false;
            }
            if (s_window)
            {
                DestroyWindow(s_window);
            }
            s_audioOwner.reset();
            s_nativeAudio = nullptr;
            CoUninitialize();
        }
        catch (...)
        {
            std::fprintf(stderr, "Original-UI cleanup failed; preserving primary failure.\n");
            static_cast<void>(bbl::report_uncaught_error(std::current_exception()));
        }
        return bbl::report_uncaught_error(error);
    }
}
