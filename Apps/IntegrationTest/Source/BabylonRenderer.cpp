#include "BabylonRenderer.h"

#include <Babylon/ScriptLoader.h>
#include <Babylon/Polyfills/Console.h>
#include <Babylon/Polyfills/Window.h>
#include <Babylon/Polyfills/XMLHttpRequest.h>
#include <Babylon/Plugins/NativeEngine.h>
#include <Babylon/Plugins/ExternalTexture.h>

#include <Windows.h>
#include <d3d11_1.h>
#include <dxgi.h>

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>

namespace
{
    const char* GetLogLevelString(Babylon::Polyfills::Console::LogLevel level)
    {
        switch (level)
        {
            case Babylon::Polyfills::Console::LogLevel::Log:
                return "Log";
            case Babylon::Polyfills::Console::LogLevel::Warn:
                return "Warn";
            case Babylon::Polyfills::Console::LogLevel::Error:
                return "Error";
            default:
                return "";
        }
    }
} // namespace

namespace IntegrationTest
{

BabylonRenderer::BabylonRenderer() = default;

BabylonRenderer::~BabylonRenderer()
{
    if (m_graphicsDevice)
    {
        m_graphicsDeviceUpdate->Finish();
        m_graphicsDevice->FinishRenderingCurrentFrame();
    }

    m_sceneContext.reset();
    m_renderTargetTexture.reset();
    m_externalTexture.reset();
    m_sharedTexture.Reset();
    m_jsRuntime.reset();
    m_graphicsDeviceUpdate.reset();
    m_graphicsDevice.reset();

    if (m_bgfxWindow)
    {
        DestroyWindow(m_bgfxWindow);
        m_bgfxWindow = nullptr;
    }
}

void BabylonRenderer::Init(const std::string& bundlePath, uint32_t width, uint32_t height)
{
    // bgfx on D3D11 requires a window handle for device creation.
    // Create a hidden message-only window so bgfx can initialize its own
    // D3D11 device without interfering with the application's swap chain.
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"BabylonBgfxHidden";
    RegisterClassExW(&wc);

    m_bgfxWindow = CreateWindowExW(
        0, L"BabylonBgfxHidden", L"", 0,
        0, 0, 1, 1, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);

    Babylon::Graphics::Configuration config{};
    config.Window = m_bgfxWindow;
    config.Width = width;
    config.Height = height;
    m_graphicsDevice = std::make_unique<Babylon::Graphics::Device>(config);
    m_graphicsDeviceUpdate = std::make_unique<Babylon::Graphics::DeviceUpdate>(
        m_graphicsDevice->GetUpdate("update"));

    BeginFrame();

    // Create JS runtime
    Babylon::AppRuntime::Options options{};
    options.EnableDebugger = false;
    options.UnhandledExceptionHandler = [](const Napi::Error& error) {
        std::ostringstream ss{};
        ss << "[Uncaught Error] " << Napi::GetErrorString(error) << std::endl;
        OutputDebugStringA(ss.str().c_str());
        std::cerr << ss.str();
    };

    m_jsRuntime = std::make_unique<Babylon::AppRuntime>(options);

    Babylon::ScriptLoader scriptLoader(*m_jsRuntime);

    // Initialize polyfills and native plugins
    scriptLoader.Dispatch([this](Napi::Env env) {
        m_graphicsDevice->AddToJavaScript(env);

        Babylon::Polyfills::Console::Initialize(env,
            [](const char* msg, auto level) {
                std::ostringstream ss{};
                ss << "[" << GetLogLevelString(level) << "] " << msg << std::endl;
                OutputDebugStringA(ss.str().c_str());
            });

        Babylon::Polyfills::Window::Initialize(env);
        Babylon::Polyfills::XMLHttpRequest::Initialize(env);
        Babylon::Plugins::NativeEngine::Initialize(env);
    });

    // Load the interop bundle
    scriptLoader.LoadScript(bundlePath);

    // Verify the bundle loaded successfully
    std::promise<void> initDone;
    scriptLoader.Dispatch([&initDone](Napi::Env env) {
        try
        {
            auto getEngineInfo = env.Global().Get("BI_getEngineInfo").As<Napi::Function>();
            auto info = getEngineInfo.Call({}).As<Napi::Object>();
            auto version = info.Get("version").As<Napi::String>().Utf8Value();
            auto name = info.Get("name").As<Napi::String>().Utf8Value();

            std::ostringstream ss;
            ss << "Babylon Engine: " << name << " v" << version << std::endl;
            OutputDebugStringA(ss.str().c_str());

            initDone.set_value();
        }
        catch (...)
        {
            initDone.set_exception(std::current_exception());
        }
    });

    initDone.get_future().get();
}

void BabylonRenderer::BeginFrame()
{
    m_graphicsDevice->StartRenderingCurrentFrame();
    m_graphicsDeviceUpdate->Start();
}

void BabylonRenderer::EndFrame()
{
    m_graphicsDeviceUpdate->Finish();
    m_graphicsDevice->FinishRenderingCurrentFrame();
}

void BabylonRenderer::DispatchToJsRuntime(
    std::function<void(Napi::Env, std::promise<void>&)>&& fn)
{
    std::promise<void> done;
    m_jsRuntime->Dispatch([&done, fn = std::move(fn)](Napi::Env env) {
        try
        {
            fn(env, done);
        }
        catch (...)
        {
            done.set_exception(std::current_exception());
        }
    });
    done.get_future().get();
}

void BabylonRenderer::LoadModel(std::vector<char> modelData, std::vector<char> environmentData)
{
    DispatchToJsRuntime([this, &modelData, &environmentData](Napi::Env env, std::promise<void>& done) {
        auto jsEnvData = Napi::ArrayBuffer::New(env, environmentData.size());
        std::memcpy(jsEnvData.Data(), environmentData.data(), jsEnvData.ByteLength());

        auto jsModelData = Napi::ArrayBuffer::New(env, modelData.size());
        std::memcpy(jsModelData.Data(), modelData.data(), jsModelData.ByteLength());

        auto createSceneAsync = env.Global().Get("BI_createSceneAsync").As<Napi::Function>();

        auto onFulfilled = Napi::Function::New(env, [this, &done](const Napi::CallbackInfo& info) {
            m_sceneContext = std::make_shared<Napi::ObjectReference>(
                Napi::Persistent(info[0].As<Napi::Object>()));
            done.set_value();
        }, "onFulfilled");

        auto onRejected = Napi::Function::New(env, [&done](const Napi::CallbackInfo& info) {
            auto err = info[0].ToString().Utf8Value();
            done.set_exception(std::make_exception_ptr(std::runtime_error{err}));
        }, "onRejected");

        auto promise = createSceneAsync.Call({jsEnvData, jsModelData}).As<Napi::Promise>();
        promise.Get("then").As<Napi::Function>().Call(promise, {onFulfilled});
        promise.Get("catch").As<Napi::Function>().Call(promise, {onRejected});
    });

    m_sceneReady = false;
}

void BabylonRenderer::BindRenderTarget(Microsoft::WRL::ComPtr<ID3D11Texture2D> outputTexture)
{
    m_outputTexture = outputTexture;

    D3D11_TEXTURE2D_DESC desc;
    m_outputTexture->GetDesc(&desc);
    m_textureWidth = desc.Width;
    m_textureHeight = desc.Height;
    m_aspectRatio = static_cast<float>(desc.Width) / static_cast<float>(desc.Height);

    // Destroy old render target if rebinding
    if (m_renderTargetTexture)
    {
        DispatchToJsRuntime([this](Napi::Env env, std::promise<void>& done) {
            env.Global().Get("BI_destroyRenderTargetTexture").As<Napi::Function>()
                .Call({m_renderTargetTexture->Value()});
            m_renderTargetTexture.reset();
            done.set_value();
        });
        m_externalTexture.reset();
        m_sharedTexture.Reset();
    }

    // Create shared texture on Babylon's internal device
    auto factoryDevice = m_graphicsDevice->GetPlatformInfo().Device;
    auto factoryD3D11Device = static_cast<ID3D11Device*>(factoryDevice);

    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    HRESULT hr = factoryD3D11Device->CreateTexture2D(&desc, nullptr, m_sharedTexture.GetAddressOf());
    assert(SUCCEEDED(hr));

    m_externalTexture = std::make_shared<Babylon::Plugins::ExternalTexture>(m_sharedTexture.Get());

    // Create JS render target texture
    std::promise<void> renderFrame;
    std::promise<void> done;

    m_jsRuntime->Dispatch([this, &renderFrame, &done](Napi::Env env) {
        auto createRT = env.Global().Get("BI_createRenderTargetTextureAsync").As<Napi::Function>();

        auto onFulfilled = Napi::Function::New(env, [this, &done](const Napi::CallbackInfo& info) {
            m_renderTargetTexture = std::make_shared<Napi::ObjectReference>(
                Napi::Persistent(info[0].As<Napi::Object>()));
            done.set_value();
        }, "onFulfilled");

        auto onRejected = Napi::Function::New(env, [&done](const Napi::CallbackInfo& info) {
            auto err = info[0].ToString().Utf8Value();
            done.set_exception(std::make_exception_ptr(std::runtime_error{err}));
        }, "onRejected");

        auto promise = createRT.Call({
            m_sceneContext->Value(),
            m_externalTexture->AddToContextAsync(env),
            Napi::Value::From(env, m_textureWidth),
            Napi::Value::From(env, m_textureHeight),
        }).As<Napi::Promise>();

        promise.Get("then").As<Napi::Function>().Call(promise, {onFulfilled});
        promise.Get("catch").As<Napi::Function>().Call(promise, {onRejected});

        renderFrame.set_value();
    });

    renderFrame.get_future().get();

    // Process the frame to fulfill the async texture creation
    EndFrame();
    BeginFrame();

    done.get_future().get();
}

void BabylonRenderer::WaitForSceneReady()
{
    std::promise<void> done;
    std::promise<void> renderFrame;

    m_jsRuntime->Dispatch([this, &done, &renderFrame](Napi::Env env) {
        auto waitFn = m_sceneContext->Get("waitForSceneReadyAsync");

        if (waitFn.IsFunction())
        {
            auto promise = waitFn.As<Napi::Function>()
                               .Call(m_sceneContext->Value(), {})
                               .As<Napi::Promise>();

            auto onFulfilled = Napi::Function::New(env, [&done](const Napi::CallbackInfo&) {
                done.set_value();
            }, "onFulfilled");

            auto onRejected = Napi::Function::New(env, [&done](const Napi::CallbackInfo& info) {
                auto err = info[0].ToString().Utf8Value();
                done.set_exception(std::make_exception_ptr(std::runtime_error{err}));
            }, "onRejected");

            promise.Get("then").As<Napi::Function>().Call(promise, {onFulfilled});
            promise.Get("catch").As<Napi::Function>().Call(promise, {onRejected});

            renderFrame.set_value();
        }
        else
        {
            renderFrame.set_value();
            done.set_value();
        }
    });

    renderFrame.get_future().get();

    // Pump render frames while waiting for the scene to become ready
    auto doneFuture = done.get_future();
    while (doneFuture.wait_for(std::chrono::milliseconds(16)) == std::future_status::timeout)
    {
        EndFrame();
        BeginFrame();
    }
    doneFuture.get();
}

void BabylonRenderer::ApplyCameraTransformJS(Napi::Env env, const ICameraTransform& cam)
{
    constexpr float pi = 3.14159265358979f;
    float fovRadians = cam.GetFovInDegree() * pi / 180.0f;

    auto fn = m_sceneContext->Get("applyCameraTransform").As<Napi::Function>();
    fn.Call(m_sceneContext->Value(), {
        Napi::Number::New(env, m_aspectRatio),       // aspectRatio
        Napi::Boolean::New(env, false),               // orthographic
        Napi::Number::New(env, fovRadians),           // fovOrOrthographicSize
        Napi::Number::New(env, cam.GetNearClip()),    // minZ
        Napi::Number::New(env, cam.GetFarClip()),     // maxZ
        Napi::Number::New(env, cam.GetPosition().x),  // positionX
        Napi::Number::New(env, cam.GetPosition().y),  // positionY
        Napi::Number::New(env, cam.GetPosition().z),  // positionZ
        Napi::Number::New(env, cam.GetTargetPoint().x), // targetX
        Napi::Number::New(env, cam.GetTargetPoint().y), // targetY
        Napi::Number::New(env, cam.GetTargetPoint().z), // targetZ
        Napi::Number::New(env, cam.GetUpVector().x),  // upVectorX
        Napi::Number::New(env, cam.GetUpVector().y),  // upVectorY
        Napi::Number::New(env, cam.GetUpVector().z),  // upVectorZ
        Napi::Number::New(env, 0.0f),                 // viewportMinX (full viewport)
        Napi::Number::New(env, 0.0f),                 // viewportMinY
        Napi::Number::New(env, 1.0f),                 // viewportMaxX
        Napi::Number::New(env, 1.0f),                 // viewportMaxY
        Napi::Number::New(env, 0.0f),                 // scissorLeft (no scissor)
        Napi::Number::New(env, 0.0f),                 // scissorTop
        Napi::Number::New(env, 0.0f),                 // scissorRight
        Napi::Number::New(env, 0.0f),                 // scissorBottom
    });
}

void BabylonRenderer::CopyRenderTextureToOutput()
{
    if (!m_outputTexture || !m_sharedTexture)
        return;

    // Open the shared texture on the application's device
    Microsoft::WRL::ComPtr<IDXGIResource> dxgiResource;
    if (FAILED(m_sharedTexture.As<IDXGIResource>(&dxgiResource)))
        return;

    HANDLE sharedHandle;
    if (FAILED(dxgiResource->GetSharedHandle(&sharedHandle)))
        return;

    Microsoft::WRL::ComPtr<ID3D11Device> appDevice;
    m_outputTexture->GetDevice(appDevice.GetAddressOf());

    Microsoft::WRL::ComPtr<ID3D11Texture2D> textureForAppAccess;
    if (FAILED(appDevice->OpenSharedResource(
            sharedHandle, __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(textureForAppAccess.GetAddressOf()))))
        return;

    // Acquire the keyed mutex on the app device side to synchronize access
    Microsoft::WRL::ComPtr<IDXGIKeyedMutex> keyedMutex;
    if (FAILED(textureForAppAccess.As<IDXGIKeyedMutex>(&keyedMutex)))
        return;

    if (FAILED(keyedMutex->AcquireSync(0, INFINITE)))
        return;

    Microsoft::WRL::ComPtr<ID3D11DeviceContext> appContext;
    appDevice->GetImmediateContext(appContext.GetAddressOf());
    appContext->CopyResource(m_outputTexture.Get(), textureForAppAccess.Get());
    appContext->Flush();

    keyedMutex->ReleaseSync(0);
}

std::vector<AnimationInfo> BabylonRenderer::GetAnimationInfos()
{
    std::vector<AnimationInfo> result;

    DispatchToJsRuntime([this, &result](Napi::Env env, std::promise<void>& done) {
        auto infos = m_sceneContext->Get("animationInfos").As<Napi::Array>();

        for (uint32_t i = 0; i < infos.Length(); i++)
        {
            auto info = infos.Get(i).As<Napi::Object>();
            AnimationInfo ai;
            ai.Name = info.Get("name").As<Napi::String>().Utf8Value();
            ai.Duration = info.Get("duration").As<Napi::Number>().FloatValue();
            result.push_back(ai);
        }

        done.set_value();
    });

    return result;
}

void BabylonRenderer::GetBoundingBox(float center[3], float extents[3])
{
    DispatchToJsRuntime([this, center, extents](Napi::Env env, std::promise<void>& done) {
        auto centerFn = m_sceneContext->Get("getBoundingBoxCenter").As<Napi::Function>();
        auto extentsFn = m_sceneContext->Get("getBoundingBoxExtents").As<Napi::Function>();

        auto jsCenter = centerFn.Call(m_sceneContext->Value(), {env.Null()}).As<Napi::Float32Array>();
        auto jsExtents = extentsFn.Call(m_sceneContext->Value(), {env.Null()}).As<Napi::Float32Array>();

        center[0] = jsCenter[0]; center[1] = jsCenter[1]; center[2] = jsCenter[2];
        extents[0] = jsExtents[0]; extents[1] = jsExtents[1]; extents[2] = jsExtents[2];

        done.set_value();
    });
}

void BabylonRenderer::Render(
    const ICameraTransform& camera, const Matrix4& sceneTransform,
    int animationIndex, float animationProgress)
{
    if (!m_sceneReady)
    {
        WaitForSceneReady();
        m_sceneReady = true;
    }

    DispatchToJsRuntime([this, &camera, &sceneTransform,
                         animationIndex, animationProgress](Napi::Env env, std::promise<void>& done) {
        // Apply animation progress if requested
        if (animationIndex >= 0)
        {
            auto applyAnim = m_sceneContext->Get("applyAnimationProgress").As<Napi::Function>();
            applyAnim.Call(m_sceneContext->Value(), {
                Napi::Number::New(env, animationIndex),
                Napi::Number::New(env, animationProgress),
            });
        }

        // Apply model transform
        auto arrayBuffer = Napi::ArrayBuffer::New(
            env, const_cast<float*>(sceneTransform.m), sizeof(Matrix4));
        auto typedArray = Napi::Float32Array::New(env, 16, arrayBuffer, 0);
        m_sceneContext->Get("applyRootNodeTransform").As<Napi::Function>()
            .Call(m_sceneContext->Value(), {typedArray});

        // Apply camera transform
        ApplyCameraTransformJS(env, camera);

        // Render scene to the render target texture
        m_sceneContext->Get("render").As<Napi::Function>()
            .Call(m_sceneContext->Value(), {m_renderTargetTexture->Value()});

        done.set_value();
    });

    // Acquire keyed mutex on Babylon's device before processing the frame.
    // This ensures exclusive access to the shared texture during rendering.
    Microsoft::WRL::ComPtr<IDXGIKeyedMutex> keyedMutex;
    m_sharedTexture.As<IDXGIKeyedMutex>(&keyedMutex);
    keyedMutex->AcquireSync(0, INFINITE);

    // RenderDoc: begin capture before GPU work so the entire Babylon render
    // stack (draw calls, state, shaders) is visible in the capture.
    m_renderDoc.BeginFrameCapture();

    // Process the GPU frame
    EndFrame();

    // RenderDoc: end capture after GPU work completes
    m_renderDoc.EndFrameCapture();

    // Release the mutex so the app device can access the texture for copying
    keyedMutex->ReleaseSync(0);

    // Copy the rendered result to the application's output texture
    CopyRenderTextureToOutput();

    // Start the next frame
    BeginFrame();
}

void BabylonRenderer::ReleaseModel()
{
    if (!m_sceneContext)
        return;

    DispatchToJsRuntime([this](Napi::Env env, std::promise<void>& done) {
        if (m_renderTargetTexture)
        {
            env.Global().Get("BI_destroyRenderTargetTexture").As<Napi::Function>()
                .Call({m_renderTargetTexture->Value()});
            m_renderTargetTexture.reset();
        }

        if (m_sceneContext)
        {
            env.Global().Get("BI_destroyScene").As<Napi::Function>()
                .Call({m_sceneContext->Value()});
            m_sceneContext.reset();
        }

        done.set_value();
    });

    m_externalTexture.reset();
    m_sharedTexture.Reset();
    m_sceneReady = false;

    // Pump a frame so bgfx processes the pending resource cleanup
    // before new resources are created in a subsequent LoadModel call.
    EndFrame();
    BeginFrame();
}

} // namespace IntegrationTest
