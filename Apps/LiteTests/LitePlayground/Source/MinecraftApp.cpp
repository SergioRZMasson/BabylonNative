#include "NativePlatform.h"
#include "NativeAudio.h"
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
    constexpr uint32_t width = 1280;
    constexpr uint32_t height = 720;
    uint32_t limit{};
    bool replay{};
    bool hidden{};
    bool windowReplay{};
    bool dialogTest{};
    std::filesystem::path captureRoot = std::filesystem::path("build") / "lite-c99" / "minecraft-run";
    for (int i = 2; i < argc; ++i)
    {
        const std::string option(argv[i]);
        if (option.starts_with("--frames="))
            limit = static_cast<uint32_t>(std::stoul(option.substr(9)));
        else if (option == "--replay")
            replay = true;
        else if (option == "--headless")
            hidden = true;
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
        else
            throw std::runtime_error("Unknown Minecraft host option: " + option);
    }
    std::filesystem::create_directories(captureRoot);
    const wchar_t* name = L"BabylonLiteFullMinecraft";
    constexpr DWORD windowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
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
    LitePlayground::NativePlatform platform(window, width, height);
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
                binding.nativeEngine.target = {BL_TARGET_SWAPCHAIN, BL_INVALID_BGFX_HANDLE, width, height, BL_COLOR_BGRA8, BL_DEPTH_D24S8, 4, 0, 64};
                binding.nativeEngine.hostUserData = &graphics;
                binding.nativeEngine.isExternalBgfxInitialized = [](void* user) { return static_cast<Graphics*>(user)->initialized; };
                binding.nativeEngine.waitForSubmittedWork = Graphics::WaitSubmitted;
                Babylon::Plugins::LiteJSBinding::Initialize(env, binding);
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
            loader.Eval(Script("platform"), "app:///Platform/platform.js");
            loader.Eval(Script("minecraft"), "app:///Demos/minecraft.ts");
        }
        const auto started = std::chrono::steady_clock::now();
        auto previous = started;
        while (!failed && !platform.Closed() && (!limit || rendered < limit))
        {
            Pump();
            if (ready && windowReplay)
            {
                platform.QueueWindowReplay(rendered + 1);
                Pump();
            }
            const auto now = std::chrono::steady_clock::now();
            const double delta = replay ? 1000.0 / 60 : std::min(100.0, std::chrono::duration<double, std::milli>(now - previous).count());
            previous = now;
            std::promise<void> framed;
            auto frameDone = framed.get_future();
            runtime.Dispatch([&](Napi::Env env) {
                try
                {
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
                    Babylon::Plugins::LiteJSBinding::Frame(env, delta);
                    if (ready)
                    {
                        ++rendered;
                        if (rendered == 180 || (replay && rendered == 240))
                        {
                            const auto path = captureRoot / (std::string(replay ? "replay-" : "frame-") + std::to_string(rendered) + ".png");
                            bgfx::requestScreenShot(BGFX_INVALID_HANDLE, path.string().c_str());
                            if (!platform.CaptureHUD(captureRoot / ("hud-" + std::to_string(rendered) + ".png")))
                            {
                                throw std::runtime_error("Native HUD capture failed.");
                            }
                        }
                    }
                    bgfx::frame();
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
                const auto status = bl_disposeRuntime(nativeRuntime);
                if (status != BL_OK)
                {
                    failed = true;
                    failure = "Full Minecraft runtime disposal failed.";
                }
                Babylon::Plugins::LiteJSBinding::Dispose(env);
                nativeRuntime = nullptr;
            }
            if (graphics.initialized)
            {
                bgfx::destroy(graphics.fence);
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
    const uint32_t requiredCaptures = limit >= 240 && replay ? 2 : limit >= 180 ? 1
                                                                                : 0;
    if (!failed && !platform.AssetFailures() && !graphics.captureFailed && (!limit || rendered >= limit) &&
        graphics.captures >= requiredCaptures && (requiredCaptures < 2 || graphics.firstCaptureHash != graphics.lastCaptureHash) &&
        (!dialogTest || platform.FileDialogsCompleted() == 2) && (!windowReplay || platform.NativeInputEvents() > 0))
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
            << ",\"captures\":" << graphics.captures.load() << ",\"assetFailures\":" << platform.AssetFailures()
            << ",\"audioSubmittedFrames\":" << audio.SubmittedFrames() << ",\"nonzeroAudioSamples\":" << audio.NonzeroSamples()
            << ",\"nativeInputEvents\":" << platform.NativeInputEvents()
            << ",\"fileDialogsCompleted\":" << platform.FileDialogsCompleted()
            << ",\"firstCaptureHash\":\"" << graphics.firstCaptureHash.load() << "\",\"lastCaptureHash\":\"" << graphics.lastCaptureHash.load() << "\""
            << ",\"audibleAudioVerified\":false,\"result\":" << result << "}\n";
    DestroyWindow(window);
    Diagnostics::SetExitCode(result);
    return result;
}
