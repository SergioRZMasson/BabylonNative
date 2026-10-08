#include <Babylon/AppRuntime.h>
#include <Babylon/ScriptLoader.h>
#include <Babylon/Plugins/LiteJSBinding.h>
#include <Babylon/Polyfills/Console.h>
#if defined(HAS_LITE_SHADER_COMPILER)
#include <babylon_lite_shader_compiler.h>
#endif
#include "Diagnostics.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <malloc.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <string>
#if defined(LITE_V8_GC_TEST)
#include <v8.h>
#endif

namespace
{
    struct Outcome
    {
        std::promise<int> promise;
        std::atomic<bool> completed{};

        void Finish(bool success, const std::string& detail)
        {
            if (completed.exchange(true))
            {
                return;
            }
            if (success)
            {
                std::printf("LitePlayground: %s\n", detail.c_str());
            }
            else
            {
                Diagnostics::DumpFailure("LITE TEST FAILURE", nullptr, 0, 0, "%s", detail.c_str());
            }
            promise.set_value(success ? 0 : 1);
        }
    };

    void* Allocate(void*, size_t bytes, size_t alignment)
    {
        return _aligned_malloc(bytes, alignment < sizeof(void*) ? sizeof(void*) : alignment);
    }

    void Deallocate(void*, void* memory, size_t, size_t)
    {
        _aligned_free(memory);
    }

    LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        return DefWindowProcW(window, message, wParam, lParam);
    }

    HWND CreateTestWindow()
    {
        const wchar_t* className = L"BabylonLiteNativeShaderFixture";
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = WindowProcedure;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = className;
        RegisterClassW(&windowClass);
        return CreateWindowW(className, L"Babylon Lite shader fixture", WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, 256, 256, nullptr, nullptr, windowClass.hInstance, nullptr);
    }

    void PumpMessages()
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
        const auto started = std::chrono::steady_clock::now();
        while (future.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready)
        {
            PumpMessages();
            if (std::chrono::steady_clock::now() - started > std::chrono::seconds(60))
            {
                throw std::runtime_error("Native host dispatch timed out.");
            }
        }
        future.get();
    }

    std::string ReadScript(const char* name)
    {
        const auto path = std::filesystem::path(LITE_SCRIPT_DIRECTORY) / (std::string(name) + ".native.js");
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            throw std::runtime_error("Missing application-only bundle: " + path.string());
        }
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }
}

int RunMinecraft(int argc, char** argv);

int main(int argc, char** argv)
{
    Diagnostics::Initialize();
    const std::string mode = argc > 1 ? argv[1] : "contract";
    if (mode == "minecraft" || mode == "minecraft-development" || mode == "ui-contract")
    {
#if defined(HAS_LITE_SHADER_COMPILER)
        try
        {
            return RunMinecraft(argc, argv);
        }
        catch (const std::exception& error)
        {
            Diagnostics::DumpFailure("LITE HOST CONFIGURATION", nullptr, 0, 0, "%s", error.what());
            Diagnostics::SetExitCode(2);
            return 2;
        }
#else
        Diagnostics::DumpFailure("LITE HOST CONFIGURATION", nullptr, 0, 0, "Full Minecraft requires LiteShaderCompiler.");
        Diagnostics::SetExitCode(2);
        return 2;
#endif
    }
    if (mode != "contract" && mode != "minecraft-shaders" && mode != "callback-throw")
    {
        std::printf("Usage: LitePlayground [contract|minecraft-shaders|callback-throw|minecraft]\n");
        Diagnostics::SetExitCode(2);
        return 2;
    }
    const bool graphics = mode != "contract";
    const bool callbackThrow = mode == "callback-throw";
#if !defined(HAS_LITE_SHADER_COMPILER)
    if (graphics)
    {
        Diagnostics::DumpFailure("LITE HOST CONFIGURATION", nullptr, 0, 0, "Shader fixtures require LiteShaderCompiler.");
        Diagnostics::SetExitCode(2);
        return 2;
    }
#endif
    HWND window = graphics ? CreateTestWindow() : nullptr;
    if (graphics && !window)
    {
        Diagnostics::DumpFailure("LITE HOST WINDOW", nullptr, 0, 0, "CreateWindowW failed (%lu).", GetLastError());
        Diagnostics::SetExitCode(1);
        return 1;
    }

    auto outcome = std::make_shared<Outcome>();
    auto result = outcome->promise.get_future();
    bl_Runtime* nativeRuntime{};
    bool bindingInitialized{};
    int exitCode = 1;
    try
    {
        Babylon::AppRuntime::Options runtimeOptions{};
        runtimeOptions.UnhandledExceptionHandler = [outcome](const Napi::Error& error) {
            outcome->Finish(false, Napi::GetErrorString(error));
        };
        Babylon::AppRuntime runtime(runtimeOptions);
        Babylon::ScriptLoader loader(runtime);
        std::promise<void> initialized;
        auto initializedFuture = initialized.get_future();
        runtime.Dispatch([&nativeRuntime, &bindingInitialized, &initialized, outcome, window](Napi::Env env) {
            try
            {
                Babylon::Polyfills::Console::Initialize(env, [env](const char* message, Babylon::Polyfills::Console::LogLevel level) {
                    if (level == Babylon::Polyfills::Console::LogLevel::Error)
                    {
                        const auto stack = Babylon::Polyfills::Console::CaptureCurrentJsStack(env);
                        Diagnostics::DumpFailure("JS CONSOLE ERROR", nullptr, 0, 0, "%s\n%s", message, stack.c_str());
                    }
                    else
                    {
                        std::printf("%s\n", message);
                    }
                });
                bl_RuntimeOptions options{};
                options.allocator = {nullptr, Allocate, Deallocate};
#if defined(HAS_LITE_SHADER_COMPILER)
                const auto compiler = bl_shaderCompilerService();
                options.shaderCompiler = &compiler;
#endif
                const auto status = bl_createRuntime(&options, &nativeRuntime);
                if (status != BL_OK)
                {
                    outcome->Finish(false, "Native runtime creation failed (status " + std::to_string(status) + ").");
                    initialized.set_value();
                    return;
                }
                Babylon::Plugins::LiteJSBinding::HostOptions host{};
                host.runtime = nativeRuntime;
                host.nativeEngine.ownership = BL_BGFX_OWNED;
                host.nativeEngine.backend = BL_RENDERER_D3D11;
                host.nativeEngine.platform.nativeWindowHandle = window;
                host.nativeEngine.target.kind = BL_TARGET_SWAPCHAIN;
                host.nativeEngine.target.framebufferIndex = BL_INVALID_BGFX_HANDLE;
                host.nativeEngine.target.width = 256;
                host.nativeEngine.target.height = 256;
                host.nativeEngine.target.colorFormat = BL_COLOR_RGBA8;
                host.nativeEngine.target.depthFormat = BL_DEPTH_D24S8;
                host.nativeEngine.target.sampleCount = 1;
                host.nativeEngine.target.viewCount = 8;
                host.nativeEngine.isExternalBgfxInitialized = [](void*) { return false; };
                Babylon::Plugins::LiteJSBinding::Initialize(env, host);
                bindingInitialized = true;
                env.Global().Set("_liteTestsDone", Napi::Function::New(env, [outcome](const Napi::CallbackInfo& info) {
                    outcome->Finish(info[0].As<Napi::Boolean>().Value(), info[1].As<Napi::String>().Utf8Value());
                }));
#if defined(LITE_V8_GC_TEST)
                env.Global().Set("_liteForceGC", Napi::Function::New(env, [](const Napi::CallbackInfo&) {
                    v8::Isolate::GetCurrent()->LowMemoryNotification();
                }));
#endif
                initialized.set_value();
            }
            catch (const Napi::Error& error)
            {
                outcome->Finish(false, Napi::GetErrorString(error));
                initialized.set_value();
            }
            catch (const std::exception& error)
            {
                outcome->Finish(false, error.what());
                initialized.set_value();
            }
        });
        Wait(initializedFuture);
        if (!outcome->completed)
        {
            const auto script = ReadScript(callbackThrow ? "callback-throw" : graphics ? "minecraft-shaders"
                                                                                       : "contract-tests");
            loader.Eval(script, "app:///LiteTests/" + mode + ".js");
        }

        const auto started = std::chrono::steady_clock::now();
        auto previous = started;
        while (result.wait_for(std::chrono::milliseconds(5)) != std::future_status::ready)
        {
            PumpMessages();
            const auto now = std::chrono::steady_clock::now();
            if (now - started > std::chrono::seconds(60))
            {
                outcome->Finish(false, "Native JavaScript fixture timed out.");
                break;
            }
            if (graphics)
            {
                const double deltaMs = std::chrono::duration<double, std::milli>(now - previous).count();
                previous = now;
                auto frame = std::make_shared<std::promise<void>>();
                auto completed = frame->get_future();
                runtime.Dispatch([frame, deltaMs, outcome, callbackThrow](Napi::Env env) {
                    try
                    {
                        Babylon::Plugins::LiteJSBinding::Frame(env, deltaMs);
                    }
                    catch (const Napi::Error& error)
                    {
                        if (callbackThrow)
                        {
                            const auto expected = env.Global().Get("_liteExpectedCallbackError");
                            const auto value = error.Value();
                            const bool original = value.StrictEquals(expected) && value.Get("marker").As<Napi::Number>().Int32Value() == 173;
                            outcome->Finish(original, original ? "Original JS exception restored after the C frame without C++ unwinding through its void callback."
                                                               : "Native callback bridge lost the original JS exception.");
                        }
                        else
                        {
                            outcome->Finish(false, Napi::GetErrorString(error));
                        }
                    }
                    frame->set_value();
                });
                Wait(completed);
            }
        }
        exitCode = result.get();
        std::promise<void> cleaned;
        auto cleanedFuture = cleaned.get_future();
        runtime.Dispatch([&nativeRuntime, &bindingInitialized, &cleaned, &exitCode, callbackThrow](Napi::Env env) {
            if (callbackThrow)
            {
                const auto rejected = env.Global().Get("_liteCallbackPromiseRejected");
                if (!rejected.IsBoolean() || !rejected.As<Napi::Boolean>().Value())
                {
                    Diagnostics::DumpFailure("LITE CALLBACK PROMISE", nullptr, 0, 0, "startEngine did not reject with the original callback exception.");
                    exitCode = 1;
                }
            }
            if (nativeRuntime)
            {
                const auto status = bl_disposeRuntime(nativeRuntime);
                if (status != BL_OK)
                {
                    Diagnostics::DumpFailure("LITE TEARDOWN", nullptr, 0, 0, "Runtime disposal failed (status %d).", status);
                    exitCode = 1;
                }
                nativeRuntime = nullptr;
                if (bindingInitialized)
                {
                    try
                    {
                        Babylon::Plugins::LiteJSBinding::Dispose(env);
                    }
                    catch (const Napi::Error& error)
                    {
                        Diagnostics::DumpFailure("LITE TEARDOWN CALLBACK", nullptr, 0, 0, "%s",
                                                Napi::GetErrorString(error).c_str());
                        exitCode = 1;
                    }
                    bindingInitialized = false;
                }
            }
            cleaned.set_value();
        });
        Wait(cleanedFuture);
    }
    catch (const std::exception& error)
    {
        Diagnostics::DumpFailure("LITE HOST EXCEPTION", nullptr, 0, 0, "%s", error.what());
        exitCode = 1;
    }
    if (window)
    {
        DestroyWindow(window);
    }
    Diagnostics::SetExitCode(exitCode);
    return exitCode;
}
