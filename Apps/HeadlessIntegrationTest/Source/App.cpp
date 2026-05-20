// HeadlessIntegrationTest
//
// Console app that mirrors the IntegrationTest JS code path but with no
// window: it creates an offscreen D3D11 render target texture, drives the
// IntegrationTest TypeScript bundle (BI_createSceneAsync /
// BI_createRenderTargetTextureAsync / SceneContext.render), then saves the
// rendered texture to a PNG next to the executable.
//
// Usage:
//   HeadlessIntegrationTest <path-to-model.glb> [output.png] [frames]
//
// All console.log / console.warn / console.error output from the JS bundle
// is routed to stdout so diagnostic logs (e.g. KHR_draco instrumentation)
// are visible in the terminal.

#include <Babylon/AppRuntime.h>
#include <Babylon/Graphics/Device.h>
#include <Babylon/Plugins/ExternalTexture.h>
#include <Babylon/Plugins/NativeEngine.h>
#include <Babylon/Polyfills/Console.h>
#include <Babylon/Polyfills/Window.h>
#include <Babylon/Polyfills/XMLHttpRequest.h>
#include <Babylon/ScriptLoader.h>

#include <winrt/base.h>
#include <ScreenGrab.h>
#include <wincodec.h>

#include <d3d11.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    constexpr uint32_t WIDTH = 1024;
    constexpr uint32_t HEIGHT = 1024;

    // Number of frames to render after the scene is "ready", giving async
    // texture/material work an opportunity to settle before snapshotting.
    constexpr uint32_t DEFAULT_FRAME_COUNT = 5;

    std::filesystem::path GetModuleDir()
    {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        return std::filesystem::path{path}.parent_path();
    }

    std::vector<char> ReadFileBytes(const std::filesystem::path& path)
    {
        std::ifstream f{path, std::ios::binary | std::ios::ate};
        if (!f) return {};
        auto size = f.tellg();
        if (size <= 0) return {};
        std::vector<char> bytes(static_cast<size_t>(size));
        f.seekg(0);
        f.read(bytes.data(), size);
        return bytes;
    }

    const char* LogLevelString(Babylon::Polyfills::Console::LogLevel level)
    {
        switch (level)
        {
            case Babylon::Polyfills::Console::LogLevel::Log: return "log";
            case Babylon::Polyfills::Console::LogLevel::Warn: return "warn";
            case Babylon::Polyfills::Console::LogLevel::Error: return "error";
        }
        return "log";
    }

    // Helper that wires a JS Promise's then/catch to a shared C++ promise.
    // Caller obtains the future BEFORE dispatching so it's safe to wait on
    // from another thread regardless of when the JS dispatch actually runs.
    template <typename TResolve>
    void HookJsPromise(Napi::Env env, Napi::Promise promise,
                       std::shared_ptr<std::promise<void>> cppPromise,
                       TResolve&& onResolve)
    {
        auto onFulfilled = Napi::Function::New(env,
            [cppPromise, onResolve = std::forward<TResolve>(onResolve)](const Napi::CallbackInfo& info) mutable
            {
                try
                {
                    onResolve(info);
                    cppPromise->set_value();
                }
                catch (...)
                {
                    cppPromise->set_exception(std::current_exception());
                }
            });
        auto onRejected = Napi::Function::New(env,
            [cppPromise](const Napi::CallbackInfo& info)
            {
                std::string msg = info[0].ToString().Utf8Value();
                cppPromise->set_exception(std::make_exception_ptr(std::runtime_error{msg}));
            });
        promise.Get("then").As<Napi::Function>().Call(promise, {onFulfilled});
        promise.Get("catch").As<Napi::Function>().Call(promise, {onRejected});
    }
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "Usage: HeadlessIntegrationTest <model.glb> [output.png] [frames]" << std::endl;
        return 2;
    }

    const std::filesystem::path modelPath = std::filesystem::absolute(argv[1]);
    const std::filesystem::path moduleDir = GetModuleDir();
    const std::filesystem::path outputPath = (argc >= 3)
        ? std::filesystem::absolute(argv[2])
        : moduleDir / (modelPath.stem().string() + ".png");
    const uint32_t frameCount = (argc >= 4)
        ? static_cast<uint32_t>(std::max(1, std::atoi(argv[3])))
        : 30;

    const std::filesystem::path bundlePath = moduleDir / "Scripts" / "BabylonInterop.bundle.js";
    const std::filesystem::path envPath = moduleDir / "assets" / "environment.env";

    std::cout << "Model:     " << modelPath.string() << std::endl;
    std::cout << "Output:    " << outputPath.string() << std::endl;
    std::cout << "Bundle:    " << bundlePath.string() << std::endl;
    std::cout << "Frames:    " << frameCount << std::endl;

    if (!std::filesystem::exists(modelPath))
    {
        std::cerr << "ERROR: model not found." << std::endl;
        return 2;
    }
    if (!std::filesystem::exists(bundlePath))
    {
        std::cerr << "ERROR: bundle not found." << std::endl;
        return 2;
    }
    if (!std::filesystem::exists(envPath))
    {
        std::cerr << "ERROR: environment.env not found." << std::endl;
        return 2;
    }

    auto modelData = ReadFileBytes(modelPath);
    auto envData = ReadFileBytes(envPath);
    if (modelData.empty() || envData.empty())
    {
        std::cerr << "ERROR: failed to read model or environment." << std::endl;
        return 2;
    }
    std::cout << "Model bytes: " << modelData.size() << ", env bytes: " << envData.size() << std::endl;

    // --- Create D3D11 device ---
    winrt::com_ptr<ID3D11Device> d3dDevice;
    winrt::com_ptr<ID3D11DeviceContext> d3dContext;
    {
        UINT flags = D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        HRESULT hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
            D3D11_SDK_VERSION, d3dDevice.put(), nullptr, d3dContext.put());
        if (FAILED(hr))
        {
            std::cerr << "D3D11CreateDevice failed: 0x" << std::hex << hr << std::endl;
            return 3;
        }
    }

    // --- Offscreen render target on the same device ---
    winrt::com_ptr<ID3D11Texture2D> outputTexture;
    {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = WIDTH;
        desc.Height = HEIGHT;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc = {4, 0};
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        winrt::check_hresult(d3dDevice->CreateTexture2D(&desc, nullptr, outputTexture.put()));
    }

    // --- Babylon graphics device wraps the existing D3D11 device ---
    Babylon::Graphics::Configuration gfxConfig{};
    gfxConfig.Device = d3dDevice.get();
    gfxConfig.Width = WIDTH;
    gfxConfig.Height = HEIGHT;
    Babylon::Graphics::Device gfxDevice{gfxConfig};
    auto gfxUpdate = gfxDevice.GetUpdate("update");

    // Open the first frame so JS can issue commands.
    gfxDevice.StartRenderingCurrentFrame();
    gfxUpdate.Start();

    // --- JS runtime ---
    Babylon::AppRuntime::Options runtimeOptions{};
    runtimeOptions.UnhandledExceptionHandler = [](const Napi::Error& error) {
        std::cerr << "[uncaught] " << Napi::GetErrorString(error) << std::endl;
    };
    Babylon::AppRuntime runtime{runtimeOptions};

    Babylon::ScriptLoader loader{runtime};

    // Polyfills must be initialized BEFORE the bundle is evaluated. Using
    // loader.Dispatch (instead of runtime.Dispatch) here guarantees this
    // setup is queued on the same queue as LoadScript so the bundle sees
    // a fully-initialized NativeEngine plugin when it executes its
    // top-level `new NativeEngine()` and `engine.getCaps()`.
    loader.Dispatch([&gfxDevice](Napi::Env env) {
        gfxDevice.AddToJavaScript(env);
        Babylon::Polyfills::Console::Initialize(env, [](const char* msg, auto level) {
            std::cout << "[" << LogLevelString(level) << "] " << msg << std::endl;
        });
        Babylon::Polyfills::Window::Initialize(env);
        Babylon::Polyfills::XMLHttpRequest::Initialize(env);
        Babylon::Plugins::NativeEngine::Initialize(env);
    });

    loader.LoadScript("file:///" + bundlePath.string());

    // --- Engine info smoke test ---
    {
        std::promise<void> done;
        loader.Dispatch([&done](Napi::Env env) {
            try
            {
                auto info = env.Global().Get("BI_getEngineInfo").As<Napi::Function>().Call({}).As<Napi::Object>();
                std::cout << "Engine: " << info.Get("name").As<Napi::String>().Utf8Value()
                          << " v" << info.Get("version").As<Napi::String>().Utf8Value() << std::endl;
                done.set_value();
            }
            catch (...) { done.set_exception(std::current_exception()); }
        });
        done.get_future().get();
    }

    // --- Create the scene (this is the call that may freeze for unsupported extensions) ---
    std::cout << "Calling BI_createSceneAsync..." << std::endl;
    std::shared_ptr<Napi::ObjectReference> sceneContext;
    {
        auto cppPromise = std::make_shared<std::promise<void>>();
        auto sceneFuture = cppPromise->get_future();
        loader.Dispatch([&sceneContext, cppPromise, &modelData, &envData](Napi::Env env) {
            auto envBuf = Napi::ArrayBuffer::New(env, envData.size());
            std::memcpy(envBuf.Data(), envData.data(), envBuf.ByteLength());
            auto modelBuf = Napi::ArrayBuffer::New(env, modelData.size());
            std::memcpy(modelBuf.Data(), modelData.data(), modelBuf.ByteLength());

            auto promise = env.Global().Get("BI_createSceneAsync").As<Napi::Function>()
                              .Call({envBuf, modelBuf}).As<Napi::Promise>();

            HookJsPromise(env, promise, cppPromise,
                [&sceneContext](const Napi::CallbackInfo& info) {
                    sceneContext = std::make_shared<Napi::ObjectReference>(
                        Napi::Persistent(info[0].As<Napi::Object>()));
                });
        });

        // Pump the GPU while we wait so any GPU-touching scene-load work can
        // make progress. If BI_createSceneAsync hangs (the partner-reported
        // freeze), we'd loop forever here -- so cap the wait.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (sceneFuture.wait_for(std::chrono::milliseconds(16)) == std::future_status::timeout)
        {
            if (std::chrono::steady_clock::now() > deadline)
            {
                std::cerr << "TIMEOUT: BI_createSceneAsync did not resolve within 60s." << std::endl;
                return 4;
            }
            gfxUpdate.Finish();
            gfxDevice.FinishRenderingCurrentFrame();
            gfxDevice.StartRenderingCurrentFrame();
            gfxUpdate.Start();
        }
        try
        {
            sceneFuture.get();
        }
        catch (const std::exception& e)
        {
            std::cerr << "BI_createSceneAsync rejected: " << e.what() << std::endl;
            return 1;
        }
        std::cout << "Scene created." << std::endl;
    }

    // --- Wrap the output texture as an external texture and create the RTT ---
    std::shared_ptr<Napi::ObjectReference> renderTarget;
    {
        auto cppPromise = std::make_shared<std::promise<void>>();
        auto rttFuture = cppPromise->get_future();
        Babylon::Plugins::ExternalTexture externalTexture{outputTexture.get()};
        loader.Dispatch([&renderTarget, cppPromise, &sceneContext, externalTexture](Napi::Env env) mutable {
            auto createRT = env.Global().Get("BI_createRenderTargetTextureAsync").As<Napi::Function>();
            auto promise = createRT.Call({
                sceneContext->Value(),
                externalTexture.AddToContextAsync(env),
                Napi::Number::New(env, WIDTH),
                Napi::Number::New(env, HEIGHT),
            }).As<Napi::Promise>();
            HookJsPromise(env, promise, cppPromise,
                [&renderTarget](const Napi::CallbackInfo& info) {
                    renderTarget = std::make_shared<Napi::ObjectReference>(
                        Napi::Persistent(info[0].As<Napi::Object>()));
                });
        });

        // Pump a frame so AddToContextAsync resolves.
        gfxUpdate.Finish();
        gfxDevice.FinishRenderingCurrentFrame();
        gfxDevice.StartRenderingCurrentFrame();
        gfxUpdate.Start();

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (rttFuture.wait_for(std::chrono::milliseconds(16)) == std::future_status::timeout)
        {
            if (std::chrono::steady_clock::now() > deadline)
            {
                std::cerr << "TIMEOUT: BI_createRenderTargetTextureAsync." << std::endl;
                return 4;
            }
            gfxUpdate.Finish();
            gfxDevice.FinishRenderingCurrentFrame();
            gfxDevice.StartRenderingCurrentFrame();
            gfxUpdate.Start();
        }
        try { rttFuture.get(); }
        catch (const std::exception& e)
        {
            std::cerr << "BI_createRenderTargetTextureAsync rejected: " << e.what() << std::endl;
            return 1;
        }
        std::cout << "Render target created." << std::endl;
    }

    // --- Wait for the scene to be fully ready (textures decoded, shaders
    //     compiled). Pumps GPU frames while waiting so async work (cube
    //     texture from .env buffer, BRDF LUT, effect compilation) can
    //     complete. Mirrors the windowed IntegrationTest's no-timeout
    //     wait, but with a generous safety bound so we don't hang the
    //     test forever if something genuinely stalls.
    {
        auto cppPromise = std::make_shared<std::promise<void>>();
        auto sceneReadyFuture = cppPromise->get_future();
        loader.Dispatch([&sceneContext, cppPromise](Napi::Env env) {
            auto waitFn = sceneContext->Get("waitForSceneReadyAsync");
            if (!waitFn.IsFunction())
            {
                cppPromise->set_value();
                return;
            }
            auto promise = waitFn.As<Napi::Function>().Call(sceneContext->Value(), {}).As<Napi::Promise>();
            HookJsPromise(env, promise, cppPromise, [](const Napi::CallbackInfo&) {});
        });

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        auto lastLog = std::chrono::steady_clock::now();
        uint64_t pumpCount = 0;
        while (sceneReadyFuture.wait_for(std::chrono::milliseconds(16)) == std::future_status::timeout)
        {
            if (std::chrono::steady_clock::now() > deadline)
            {
                std::cout << "whenReadyAsync did not settle in 10s (pumped " << pumpCount << " frames); proceeding anyway." << std::endl;
                break;
            }
            gfxUpdate.Finish();
            gfxDevice.FinishRenderingCurrentFrame();
            gfxDevice.StartRenderingCurrentFrame();
            gfxUpdate.Start();
            ++pumpCount;
            if (std::chrono::steady_clock::now() - lastLog > std::chrono::seconds(2))
            {
                std::cout << "  still waiting on whenReadyAsync... pumped=" << pumpCount << std::endl;
                lastLog = std::chrono::steady_clock::now();
            }
        }
        if (sceneReadyFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
        {
            try { sceneReadyFuture.get(); }
            catch (const std::exception& e) { std::cerr << "whenReadyAsync rejected: " << e.what() << std::endl; }
        }
        std::cout << "Proceeding to render loop (pumps=" << pumpCount << ")." << std::endl;
    }

    // --- Query bounding box and frame the camera ---
    float center[3] = {0, 0, 0};
    float extents[3] = {1, 1, 1};
    {
        std::promise<void> done;
        loader.Dispatch([&](Napi::Env env) {
            try
            {
                auto centerFn = sceneContext->Get("getBoundingBoxCenter").As<Napi::Function>();
                auto extentsFn = sceneContext->Get("getBoundingBoxExtents").As<Napi::Function>();
                auto c = centerFn.Call(sceneContext->Value(), {env.Null()}).As<Napi::Float32Array>();
                auto e = extentsFn.Call(sceneContext->Value(), {env.Null()}).As<Napi::Float32Array>();
                center[0] = c[0]; center[1] = c[1]; center[2] = c[2];
                extents[0] = e[0]; extents[1] = e[1]; extents[2] = e[2];
                done.set_value();
            }
            catch (...) { done.set_exception(std::current_exception()); }
        });
        try { done.get_future().get(); }
        catch (const std::exception& ex)
        {
            std::cerr << "Bounding box query failed: " << ex.what() << std::endl;
            return 1;
        }
    }
    std::cout << "Bounds: center=(" << center[0] << "," << center[1] << "," << center[2] << ") "
              << "extents=(" << extents[0] << "," << extents[1] << "," << extents[2] << ")" << std::endl;

    const float fovDeg = 45.0f;
    const float fovRad = fovDeg * 3.14159265f / 180.0f;
    float maxExtent = std::max({extents[0], extents[1], extents[2], 0.001f});
    float distance = maxExtent / std::tan(fovRad * 0.5f) * 1.5f;
    // Look down -Z towards the model center.
    const float camPos[3] = {center[0], center[1], center[2] + distance};
    const float target[3] = {center[0], center[1], center[2]};
    const float upVec[3]  = {0.0f, 1.0f, 0.0f};

    // --- Render N frames ---
    for (uint32_t frame = 0; frame < frameCount; ++frame)
    {
        std::promise<void> done;
        loader.Dispatch([&](Napi::Env env) {
            try
            {
                // Identity root-node transform.
                float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
                auto buf = Napi::ArrayBuffer::New(env, identity, sizeof(identity));
                auto arr = Napi::Float32Array::New(env, 16, buf, 0);
                sceneContext->Get("applyRootNodeTransform").As<Napi::Function>()
                    .Call(sceneContext->Value(), {arr});

                auto applyCam = sceneContext->Get("applyCameraTransform").As<Napi::Function>();
                applyCam.Call(sceneContext->Value(), {
                    Napi::Number::New(env, static_cast<float>(WIDTH) / HEIGHT),
                    Napi::Boolean::New(env, false),
                    Napi::Number::New(env, fovRad),
                    Napi::Number::New(env, distance * 0.01f),
                    Napi::Number::New(env, distance * 100.0f),
                    Napi::Number::New(env, camPos[0]),
                    Napi::Number::New(env, camPos[1]),
                    Napi::Number::New(env, camPos[2]),
                    Napi::Number::New(env, target[0]),
                    Napi::Number::New(env, target[1]),
                    Napi::Number::New(env, target[2]),
                    Napi::Number::New(env, upVec[0]),
                    Napi::Number::New(env, upVec[1]),
                    Napi::Number::New(env, upVec[2]),
                    Napi::Number::New(env, 0.0f),
                    Napi::Number::New(env, 0.0f),
                    Napi::Number::New(env, 1.0f),
                    Napi::Number::New(env, 1.0f),
                    Napi::Number::New(env, 0.0f),
                    Napi::Number::New(env, 0.0f),
                    Napi::Number::New(env, 0.0f),
                    Napi::Number::New(env, 0.0f),
                });

                sceneContext->Get("render").As<Napi::Function>()
                    .Call(sceneContext->Value(), {renderTarget->Value()});

                done.set_value();
            }
            catch (...) { done.set_exception(std::current_exception()); }
        });
        try { done.get_future().get(); }
        catch (const std::exception& ex)
        {
            std::cerr << "Frame " << frame << " JS dispatch failed: " << ex.what() << std::endl;
            return 1;
        }

        gfxUpdate.Finish();
        gfxDevice.FinishRenderingCurrentFrame();
        std::cout << "Frame " << frame << " rendered." << std::endl;

        if (frame + 1 < frameCount)
        {
            gfxDevice.StartRenderingCurrentFrame();
            gfxUpdate.Start();
        }
    }

    // --- Save PNG ---
    std::cout << "Saving PNG to " << outputPath.string() << std::endl;
    HRESULT hr = DirectX::SaveWICTextureToFile(
        d3dContext.get(), outputTexture.get(),
        GUID_ContainerFormatPng, outputPath.wstring().c_str(),
        nullptr, nullptr, true);
    if (FAILED(hr))
    {
        std::cerr << "SaveWICTextureToFile failed: 0x" << std::hex << hr << std::endl;
        return 5;
    }

    std::cout << "OK" << std::endl;
    return 0;
}
