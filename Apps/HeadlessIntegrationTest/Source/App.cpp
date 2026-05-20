// HeadlessIntegrationTest
//
// Console app that renders a .glb to a PNG without a window, using the
// UMD babylon.max.js bundle (same proven path as HeadlessScreenshotApp).
//
// Usage: HeadlessIntegrationTest <model.glb> [output.png]

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

#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    constexpr uint32_t WIDTH = 1024;
    constexpr uint32_t HEIGHT = 1024;

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

    void CatchAndLogError(Napi::Promise jsPromise)
    {
        auto jsOnRejected = Napi::Function::New(jsPromise.Env(), [](const Napi::CallbackInfo& info) {
            auto console = info.Env().Global().Get("console").As<Napi::Object>();
            console.Get("error").As<Napi::Function>().Call(console, {info[0]});
            std::exit(1);
        });
        jsPromise.Get("catch").As<Napi::Function>().Call(jsPromise, {jsOnRejected});
    }
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "Usage: HeadlessIntegrationTest <model.glb> [output.png]" << std::endl;
        return 2;
    }

    const std::filesystem::path modelPath = std::filesystem::absolute(argv[1]);
    const std::filesystem::path moduleDir = GetModuleDir();
    const std::filesystem::path outputPath = (argc >= 3)
        ? std::filesystem::absolute(argv[2])
        : moduleDir / (modelPath.stem().string() + ".png");

    std::cout << "Model:  " << modelPath.string() << std::endl;
    std::cout << "Output: " << outputPath.string() << std::endl;

    if (!std::filesystem::exists(modelPath)) { std::cerr << "ERROR: model not found." << std::endl; return 2; }

    auto modelData = ReadFileBytes(modelPath);
    if (modelData.empty()) { std::cerr << "ERROR: failed to read model." << std::endl; return 2; }

    // --- D3D11 device ---
    winrt::com_ptr<ID3D11Device> d3dDevice;
    winrt::com_ptr<ID3D11DeviceContext> d3dContext;
    {
        UINT flags = D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        winrt::check_hresult(D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
            D3D11_SDK_VERSION, d3dDevice.put(), nullptr, d3dContext.put()));
    }

    // --- Offscreen render target ---
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

    // --- Babylon graphics device (reuses our D3D11 device) ---
    Babylon::Graphics::Configuration gfxConfig{};
    gfxConfig.Device = d3dDevice.get();
    gfxConfig.Width = WIDTH;
    gfxConfig.Height = HEIGHT;
    Babylon::Graphics::Device gfxDevice{gfxConfig};
    auto gfxUpdate = gfxDevice.GetUpdate("update");

    gfxDevice.StartRenderingCurrentFrame();
    gfxUpdate.Start();

    // --- JS runtime + polyfills + scripts ---
    Babylon::AppRuntime runtime{};
    runtime.Dispatch([&gfxDevice](Napi::Env env) {
        gfxDevice.AddToJavaScript(env);
        Babylon::Polyfills::Console::Initialize(env, [](const char* msg, auto level) {
            std::cout << "[" << LogLevelString(level) << "] " << msg << std::endl;
        });
        Babylon::Polyfills::Window::Initialize(env);
        Babylon::Polyfills::XMLHttpRequest::Initialize(env);
        Babylon::Plugins::NativeEngine::Initialize(env);
    });

    Babylon::ScriptLoader loader{runtime};
    loader.LoadScript("app:///Scripts/babylon.max.js");
    loader.LoadScript("app:///Scripts/babylonjs.loaders.js");
    loader.LoadScript("app:///Scripts/index.js");

    // ===== PHASE 1: startup =====
    // Call AddToContextAsync, signal when it's been queued, then pump
    // one frame so the texture handle resolves, then wait for startup().
    std::promise<void> addToContext{};
    std::promise<void> startup{};

    loader.Dispatch([externalTexture = Babylon::Plugins::ExternalTexture{outputTexture.get()},
                     &addToContext, &startup](Napi::Env env) {
        auto jsPromise = externalTexture.AddToContextAsync(env);
        addToContext.set_value();

        auto jsOnFulfilled = Napi::Function::New(env, [&startup](const Napi::CallbackInfo& info) {
            auto nativeTexture = info[0];
            info.Env().Global().Get("startup").As<Napi::Function>().Call({
                nativeTexture,
                Napi::Value::From(info.Env(), WIDTH),
                Napi::Value::From(info.Env(), HEIGHT),
            });
            startup.set_value();
        });

        jsPromise = jsPromise.Get("then").As<Napi::Function>().Call(jsPromise, {jsOnFulfilled}).As<Napi::Promise>();
        CatchAndLogError(jsPromise);
    });

    addToContext.get_future().wait();

    // Render one frame to settle AddToContextAsync.
    gfxUpdate.Finish();
    gfxDevice.FinishRenderingCurrentFrame();

    startup.get_future().wait();

    // ===== PHASE 2: load + render =====
    gfxDevice.StartRenderingCurrentFrame();
    gfxUpdate.Start();

    std::promise<void> loadAndRender{};

    loader.Dispatch([&loadAndRender, &modelData](Napi::Env env) {
        std::cout << "Loading model (" << modelData.size() << " bytes)" << std::endl;

        auto modelBuf = Napi::ArrayBuffer::New(env, modelData.size());
        std::memcpy(modelBuf.Data(), modelData.data(), modelBuf.ByteLength());

        auto jsPromise = env.Global().Get("loadAndRenderModelAsync").As<Napi::Function>()
            .Call({modelBuf}).As<Napi::Promise>();

        auto jsOnFulfilled = Napi::Function::New(env, [&loadAndRender](const Napi::CallbackInfo&) {
            loadAndRender.set_value();
        });

        jsPromise = jsPromise.Get("then").As<Napi::Function>().Call(jsPromise, {jsOnFulfilled}).As<Napi::Promise>();
        CatchAndLogError(jsPromise);
    });

    loadAndRender.get_future().wait();

    // Flush the render commands.
    gfxUpdate.Finish();
    gfxDevice.FinishRenderingCurrentFrame();

    // --- Save PNG ---
    std::cout << "Saving " << outputPath.string() << std::endl;
    winrt::check_hresult(DirectX::SaveWICTextureToFile(
        d3dContext.get(), outputTexture.get(),
        GUID_ContainerFormatPng, outputPath.wstring().c_str(),
        nullptr, nullptr, true));

    std::cout << "OK" << std::endl;
    return 0;
}
