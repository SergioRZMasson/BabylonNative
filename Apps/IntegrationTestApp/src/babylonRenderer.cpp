#include <babylonRenderer.h>
#include <GraphicsDebug.h>

#include <Babylon/ScriptLoader.h>
#include <Babylon/Polyfills/Console.h>
#include <Babylon/Polyfills/Window.h>
#include <Babylon/Polyfills/XMLHttpRequest.h>
#include <Babylon/Plugins/NativeEngine.h>
#include <Babylon/Plugins/ExternalTexture.h>

#include <Windows.h>
#include <iostream>
#include <ostream>
#include <sstream>
#include <assert.h>
#include <math.h>


const char* GetLogLevelString(Babylon::Polyfills::Console::LogLevel logLevel)
{
    switch (logLevel)
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

BabylonRenderer::BabylonRenderer(ID3D11Device* device, ID3D11DeviceContext* context)
    : m_device{device}
    , m_deviceContext{context}
{
}

void BabylonRenderer::BeginFrame()
{
    BABYLON_GRAPHICS_DEBUG_BEGIN_FRAME_CAPTURE(m_pGraphicsDevice->GetPlatformInfo().Device);
    m_pGraphicsDevice->StartRenderingCurrentFrame();
    m_pGraphicsDeviceUpdate->Start();
}

void BabylonRenderer::EndFrame()
{
    m_pGraphicsDeviceUpdate->Finish();
    m_pGraphicsDevice->FinishRenderingCurrentFrame();
    BABYLON_GRAPHICS_DEBUG_END_FRAME_CAPTURE(m_pGraphicsDevice->GetPlatformInfo().Device);
}

void BabylonRenderer::RenderFrame()
{
    EndFrame();
    BeginFrame();
}

void BabylonRenderer::RenderJS(Napi::Env env)
{
    auto jsRender = m_pContext->Get("render").As<Napi::Function>();
    jsRender.Call(m_pContext->Value(), {m_pRenderTargetTexture->Value()});
}

void BabylonRenderer::ApplyRootNodeTransform(Napi::Env env, const Matrix4& transform)
{
    auto arrayBuffer = Napi::ArrayBuffer::New(env, const_cast<Matrix4*>(&transform), sizeof(transform));
    auto typedArray = Napi::Float32Array::New(env, 16, arrayBuffer, 0);
    auto applyRootNodeTransform = m_pContext->Get("applyRootNodeTransform").As<Napi::Function>();
    applyRootNodeTransform.Call(m_pContext->Value(), {typedArray});
}

void BabylonRenderer::ApplyCameraTransform(Napi::Env env, const ICameraTransform& transform,
    float left, float top, float right, float bottom, bool fClipped)
{
    float viewportScaleForMargin = 1.0f;
    if (!fClipped)
    {
        const float maxMarginX = std::max(left, static_cast<float>(m_textureWidth) - right) / (right - left);
        const float maxMarginY = std::max(top, static_cast<float>(m_textureHeight) - bottom) / (bottom - top);
        viewportScaleForMargin = 1.0f + std::max(maxMarginX, maxMarginY) * 2;
    }

    const float centerX = (left + right) / 2.0f;
    const float centerY = (top + bottom) / 2.0f;
    const float viewportWidth = static_cast<float>(right - left) * viewportScaleForMargin;
    const float viewportHeight = static_cast<float>(bottom - top) * viewportScaleForMargin;

    const float vpMinX = centerX - (viewportWidth / 2.0f);
    const float vpMinY = centerY - (viewportHeight / 2.0f);
    const float vpMaxX = centerX + (viewportWidth / 2.0f);
    const float vpMaxY = centerY + (viewportHeight / 2.0f);

    const bool fOrthographic = false;
    float cameraFovOrOrthographicSize{};

    constexpr float pi = 3.14;
    const float verticalFieldOfViewDegrees = transform.GetFovInDegree();
    const float verticalFieldOfViewDegreesWithPadding = 2.0f * atanf(viewportScaleForMargin * tanf(verticalFieldOfViewDegrees / 2.0f * pi / 180.0f)) * 180.0f / pi;
    cameraFovOrOrthographicSize = (verticalFieldOfViewDegreesWithPadding / 360.0f) * 2.0f * pi;

    if (!fClipped)
    {
        left = top = right = bottom = 0.0f;
    }

    auto applyCameraTransform = m_pContext->Get("applyCameraTransform").As<Napi::Function>();
    applyCameraTransform.Call(m_pContext->Value(),
        {Napi::Value::From(env, m_textureAspectRatio),
            Napi::Value::From(env, fOrthographic),
            Napi::Value::From(env, cameraFovOrOrthographicSize),
            Napi::Value::From(env, transform.GetNearClip()),
            Napi::Value::From(env, transform.GetFarClip()),
            Napi::Value::From(env, transform.GetPosition().x),
            Napi::Value::From(env, transform.GetPosition().y),
            Napi::Value::From(env, transform.GetPosition().z),
            Napi::Value::From(env, transform.GetTargetPoint().x),
            Napi::Value::From(env, transform.GetTargetPoint().y),
            Napi::Value::From(env, transform.GetTargetPoint().z),
            Napi::Value::From(env, transform.GetUpVector().x),
            Napi::Value::From(env, transform.GetUpVector().y),
            Napi::Value::From(env, transform.GetUpVector().z),
            Napi::Value::From(env, vpMinX / m_textureWidth),
            Napi::Value::From(env, vpMinY / m_textureHeight),
            Napi::Value::From(env, vpMaxX / m_textureWidth),
            Napi::Value::From(env, vpMaxY / m_textureHeight),
            Napi::Value::From(env, left),
            Napi::Value::From(env, top),
            Napi::Value::From(env, right),
            Napi::Value::From(env, bottom)});
}

void BabylonRenderer::DispatchToJsRuntime(std::function<void(Napi::Env, std::promise<void>&)>&& function) const
{
    std::promise<void> done;

    m_pJsRuntime->Dispatch([&done, function = std::move(function)](Napi::Env env) {
        try
        {
            function(env, done);
        }
        catch (...)
        {
            done.set_exception(std::current_exception());
        }
    });

    done.get_future().get();
}

void BabylonRenderer::Render(const Rect& viewport, const Matrix4& sceneTransform, const ICameraTransform& cameraTransform, bool fClipped)
{
    WaitForSceneReady();

    DispatchToJsRuntime([this, &sceneTransform, &viewport, &cameraTransform, fClipped](Napi::Env env, std::promise<void>& done) {
        ApplyRootNodeTransform(env, sceneTransform);
        ApplyCameraTransform(env, cameraTransform, viewport.Left(), viewport.Top(), viewport.Right(), viewport.Bottom(), fClipped);
        RenderJS(env);
        done.set_value();
    });

    RenderFrame();

    CopyRenderTextureToOutput();
    EndFrame();
}

void BabylonRenderer::LoadModel3D(std::vector<char> modelData, std::vector<char> environmentData)
{
    DispatchToJsRuntime([this, &modelData, environmentData](Napi::Env env, std::promise<void>& done) {
        auto jsEnvironmentData = Napi::ArrayBuffer::New(env, environmentData.size());
        std::memcpy(jsEnvironmentData.Data(), environmentData.data(), jsEnvironmentData.ByteLength());

        auto jsModelData = Napi::ArrayBuffer::New(env, modelData.size());
        std::memcpy(jsModelData.Data(), modelData.data(), jsModelData.ByteLength());

        auto createSceneAsync = env.Global().Get("BI_createSceneAsync").As<Napi::Function>();

        auto onFulfilled = [this, &done](const Napi::CallbackInfo& info) {
            auto context = info[0].As<Napi::Object>();
            m_pContext = std::make_shared<Napi::ObjectReference>(Napi::Persistent(context));
            done.set_value();
        };

        auto onRejected = [&done](const Napi::CallbackInfo& info) {
            auto errorString = info[0].ToString().Utf8Value();
            assert(false);
            done.set_exception(std::make_exception_ptr(std::runtime_error{errorString}));
        };

        auto promise = createSceneAsync.Call({jsEnvironmentData, jsModelData}).As<Napi::Promise>();
        promise.Get("then").As<Napi::Function>().Call(promise, {Napi::Function::New(env, onFulfilled, "onFulfilled")});
        promise.Get("catch").As<Napi::Function>().Call(promise, {Napi::Function::New(env, onRejected, "onRejected")});
    });
}

void BabylonRenderer::Init()
{
    BABYLON_GRAPHICS_DEBUG_INIT();

    if (m_pGraphicsDevice)
    {
        return;
    }

    ::Babylon::Graphics::Configuration config;

    m_pGraphicsDevice = std::make_unique<::Babylon::Graphics::Device>(config);
    m_pGraphicsDeviceUpdate = std::make_unique<::Babylon::Graphics::DeviceUpdate>(m_pGraphicsDevice->GetUpdate("update"));

    // Initialize with the frame started so that JS will not block when calling native engine APIs.
    BeginFrame();

    auto jsOptions = ::Babylon::AppRuntime::Options{};
    jsOptions.EnableDebugger = false;
    jsOptions.WaitForDebugger = false;

    m_pJsRuntime = std::make_unique<Babylon::AppRuntime>(jsOptions);

    ::Babylon::ScriptLoader scriptLoader(*m_pJsRuntime);
    std::promise<void> done;

    scriptLoader.Dispatch([this](Napi::Env env) {
        m_pGraphicsDevice->AddToJavaScript(env);

        ::Babylon::Polyfills::Console::Initialize(env,
            [](const char* message, auto logLevel) {
                std::ostringstream ss{};
                ss << "[" << GetLogLevelString(logLevel) << "] " << message << std::endl;
                OutputDebugStringA(ss.str().data());
                std::cout << ss.str();
                std::cout.flush();
            });

        ::Babylon::Polyfills::Window::Initialize(env);
        ::Babylon::Polyfills::XMLHttpRequest::Initialize(env);
        ::Babylon::Plugins::NativeEngine::Initialize(env);
    });

    scriptLoader.LoadScript("http://127.0.0.1:8080/BabylonInterop.bundle.js");

    scriptLoader.Dispatch([this, &done](Napi::Env env) {
        auto getEngineInfo = env.Global().Get("BI_getEngineInfo").As<Napi::Function>();
        auto engineInfo = getEngineInfo.Call({}).As<Napi::Object>();
        m_engineStats.engineVersion = engineInfo.Get("version").As<Napi::String>().Utf8Value();
        m_engineStats.engineName = engineInfo.Get("name").As<Napi::String>().Utf8Value();

        done.set_value();
    });

    // Wait for script loader to complete before continuing.
    done.get_future().get();
}

void BabylonRenderer::WaitForSceneReady()
{
    std::promise<void> done;
    std::promise<void> renderFrame;

    m_pJsRuntime->Dispatch([this, &done, &renderFrame](Napi::Env env) {
        auto jsRender = m_pContext->Get("waitForSceneReadyAsync").As<Napi::Function>();
        auto promise = jsRender.Call(m_pContext->Value(), {}).As<Napi::Promise>();

        auto onFulfilled = Napi::Function::New(env, [this, &done](const Napi::CallbackInfo&) {done.set_value(); }, "onFulfilled");

        auto onRejected = Napi::Function::New(env, [&done](const Napi::CallbackInfo& info) {
                auto errorString = info[0].ToString().Utf8Value();
                assert(false);
                done.set_exception(std::make_exception_ptr(std::runtime_error { errorString })); }, "onRejected");

        promise.Get("then").As<Napi::Function>().Call(promise, {onFulfilled});
        promise.Get("catch").As<Napi::Function>().Call(promise, {onRejected});

        renderFrame.set_value();
    });

    renderFrame.get_future().get();

    std::future<void> doneFuture = done.get_future();

    while (std::future_status::timeout == doneFuture.wait_for(std::chrono::milliseconds(16)))
    {
        RenderFrame();
    }
    
    doneFuture.get();
}

void BabylonRenderer::CopyRenderTextureToOutput()
{
    // Office will not use a GPU texture to get the rendering result from Babylon.
    if (m_poutputRenderTexture == nullptr)
    {
        return;
    }

    Microsoft::WRL::ComPtr<ID3D11Device> factoryDevice;
    m_pBabylonRenderTexture->GetDevice(factoryDevice.GetAddressOf());

    Microsoft::WRL::ComPtr<ID3D11Device> officeProvidedDevice;
    m_poutputRenderTexture->GetDevice(officeProvidedDevice.GetAddressOf());

    Microsoft::WRL::ComPtr<ID3D11DeviceContext> factoryContext;
    factoryDevice->GetImmediateContext(factoryContext.GetAddressOf());
    factoryContext->Flush();

    Microsoft::WRL::ComPtr<IDXGIResource> pOtherResource(nullptr);
    FAILED(m_pBabylonRenderTexture.As<IDXGIResource>(&pOtherResource));

    HANDLE sharedHandle;
    FAILED(pOtherResource->GetSharedHandle(&sharedHandle));

    Microsoft::WRL::ComPtr<ID3D11Resource> tempResource;

    // Try to open Babylon texture in Office's device (for GPU copy)
    if (FAILED(officeProvidedDevice->OpenSharedResource(sharedHandle, __uuidof(ID3D11Resource), (void**)tempResource.GetAddressOf())))
    {
        // Fallback to CPU copy (when Office is using a different physical device than Babylon)
        assert(false);
    }
    else
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> tempTexture;
        FAILED(tempResource.As<ID3D11Texture2D>(&tempTexture));
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> deviceContext;
        officeProvidedDevice->GetImmediateContext(deviceContext.GetAddressOf());

        // Copy Babylon texture content to Office's texture using GPU.
        deviceContext->CopyResource(m_poutputRenderTexture, tempTexture.Get());
    }
}



void BabylonRenderer::SetRenderTarget(ID3D11Texture2D* texture)
{
    m_poutputRenderTexture = texture;
    auto factoryDevice = m_pGraphicsDevice->GetPlatformInfo().Device;
    auto factoryD3D11Device = static_cast<ID3D11Device*>(factoryDevice);

    // Create a Render Texture that can be shared by both Office's and Babylon's devices.
    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    FAILED(factoryD3D11Device->CreateTexture2D(&desc, nullptr, m_pBabylonRenderTexture.GetAddressOf()));

    m_pBabylonExternalTexture = std::make_shared<::Babylon::Plugins::ExternalTexture>(m_pBabylonRenderTexture.Get());

    std::promise<void> renderFrame;
    std::promise<void> done;

    SetTextureSize(desc.Width, desc.Height);

    m_pJsRuntime->Dispatch([this, &renderFrame, &done](Napi::Env env) {
        auto createRenderTargetTextureAsync = env.Global().Get("BI_createRenderTargetTextureAsync").As<Napi::Function>();

        auto onFulfilled = Napi::Function::New(env, [this, &done](const Napi::CallbackInfo& info) {
                m_pRenderTargetTexture = std::make_shared<Napi::ObjectReference>(Napi::Persistent(info[0].As<Napi::Object>()));
                done.set_value(); }, "onFulfilled");

        auto onRejected = Napi::Function::New(env, [&done](const Napi::CallbackInfo& info) {
                auto errorString = info[0].ToString().Utf8Value();
                assert(false);
                done.set_exception(std::make_exception_ptr(std::runtime_error { errorString })); }, "onRejected");

        auto promise = createRenderTargetTextureAsync
                           .Call({
                               m_pContext->Value(),
                               m_pBabylonExternalTexture->AddToContextAsync(env),
                               Napi::Value::From(env, TextureWidth()),
                               Napi::Value::From(env, TextureHeight()),
                           })
                           .As<Napi::Promise>();
        promise.Get("then").As<Napi::Function>().Call(promise, {onFulfilled});
        promise.Get("catch").As<Napi::Function>().Call(promise, {onRejected});

        renderFrame.set_value();
    });

    renderFrame.get_future().get();
    RenderFrame();
    done.get_future().get();
}
