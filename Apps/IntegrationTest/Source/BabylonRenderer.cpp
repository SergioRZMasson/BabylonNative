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

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>

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

    void ThrowIfFailed(HRESULT hr)
    {
        if (FAILED(hr))
        {
            std::ostringstream ss;
            ss << "D3D11 call failed: 0x" << std::hex << static_cast<unsigned>(hr);
            throw std::runtime_error(ss.str());
        }
    }

    // MSAA sample count for the Babylon render target. The shared and CPU-copy paths must stay single-sample
    // (D3D11 can neither share nor CopyResource a multisampled texture), so Babylon renders into a separate
    // multisampled texture that we resolve into the single-sample render texture each frame. Antialiasing
    // keeps detailed models from aliasing and appearing to shrink/shimmer while rotating.
    constexpr UINT c_msaaSampleCount = 4;

    // Babylon.js Texture sampling modes (Texture.*_SAMPLINGMODE), passed to createRenderTargetTexture. The
    // render textures have a single mip level (and MSAA textures cannot have mips), so bilinear is correct.
    constexpr int c_babylonBilinearSamplingMode = 2;  // Texture.BILINEAR_SAMPLINGMODE
    constexpr int c_babylonTrilinearSamplingMode = 3; // Texture.TRILINEAR_SAMPLINGMODE

    // Creates and owns the dedicated D3D11 device Babylon Native renders with. BGRA support is requested for
    // interop with device paths that expect BGRA. Falls back to the WARP software rasterizer when no
    // compatible hardware device is available.
    Microsoft::WRL::ComPtr<ID3D11Device> CreateBabylonD3D11Device()
    {
        static const D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };

        const UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

        Microsoft::WRL::ComPtr<ID3D11Device> device;
        HRESULT hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, creationFlags,
            featureLevels, _countof(featureLevels), D3D11_SDK_VERSION,
            device.ReleaseAndGetAddressOf(), nullptr, nullptr);

        if (FAILED(hr))
        {
            hr = D3D11CreateDevice(
                nullptr, D3D_DRIVER_TYPE_WARP, nullptr, creationFlags,
                featureLevels, _countof(featureLevels), D3D11_SDK_VERSION,
                device.ReleaseAndGetAddressOf(), nullptr, nullptr);
        }

        ThrowIfFailed(hr);
        return device;
    }

    // Creates a multisampled render texture matching the given size/format, or returns null if the device
    // does not support the requested sample count for that format (rendering then stays single-sample).
    Microsoft::WRL::ComPtr<ID3D11Texture2D> CreateMsaaRenderTexture(
        ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format)
    {
        UINT qualityLevels = 0;
        if (FAILED(device->CheckMultisampleQualityLevels(format, c_msaaSampleCount, &qualityLevels)) ||
            qualityLevels == 0)
        {
            return nullptr;
        }

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = format;
        desc.SampleDesc.Count = c_msaaSampleCount;
        desc.SampleDesc.Quality = 0;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        ThrowIfFailed(device->CreateTexture2D(&desc, nullptr, texture.GetAddressOf()));
        return texture;
    }
} // namespace

namespace IntegrationTest
{

BabylonRenderer::BabylonRenderer() = default;

BabylonRenderer::~BabylonRenderer()
{
    if (m_graphicsDevice)
    {
        m_graphicsDevice->FinishRenderingCurrentFrame();
    }

    m_sceneContext.reset();
    m_renderTargetTexture.reset();
    m_externalTexture.reset();
    m_msaaTexture.Reset();
    m_sharedTexture.Reset();
    m_jsRuntime.reset();
    m_graphicsDevice.reset();
    m_babylonDevice.Reset();
}

void BabylonRenderer::Init(const std::string& bundlePath, std::vector<char> environmentData, uint32_t width, uint32_t height)
{
    // Create and own the D3D11 device Babylon Native renders with, and provide it (plus a non-zero initial
    // render size) via Graphics::Configuration::Device. No window/HWND is used: rendering always targets an
    // offscreen render target texture whose result is copied to the application's own texture.
    m_babylonDevice = CreateBabylonD3D11Device();

    Babylon::Graphics::Configuration config{};
    config.Device = m_babylonDevice.Get();
    config.Width = width;
    config.Height = height;
    m_graphicsDevice = std::make_unique<Babylon::Graphics::Device>(config);

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

    // Upload the environment/IBL map once (shared by every scene) and verify the bundle loaded successfully.
    std::promise<void> initDone;
    scriptLoader.Dispatch([&initDone, &environmentData](Napi::Env env) {
        try
        {
            auto jsEnvData = Napi::ArrayBuffer::New(env, environmentData.size());
            std::memcpy(jsEnvData.Data(), environmentData.data(), jsEnvData.ByteLength());
            env.Global().Get("BI_setEnvironment").As<Napi::Function>().Call({jsEnvData});

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
}

void BabylonRenderer::EndFrame()
{
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

void BabylonRenderer::LoadModel(std::vector<char> modelData)
{
    std::promise<void> done;

    m_jsRuntime->Dispatch([this, &modelData, &done](Napi::Env env) {
        try
        {
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

            auto promise = createSceneAsync.Call({jsModelData}).As<Napi::Promise>();
            promise.Get("then").As<Napi::Function>().Call(promise, {onFulfilled});
            promise.Get("catch").As<Napi::Function>().Call(promise, {onRejected});
        }
        catch (...)
        {
            done.set_exception(std::current_exception());
        }
    });

    // The scene loads asynchronously, and createSceneAsync also waits for transmissive (KHR_materials_transmission)
    // materials to finish shader compilation before it resolves. Pump graphics frames while we wait so Babylon
    // Native's background shader compilation - which that readiness check depends on - can make progress.
    auto doneFuture = done.get_future();
    while (doneFuture.wait_for(std::chrono::milliseconds(16)) == std::future_status::timeout)
    {
        EndFrame();
        BeginFrame();
    }
    doneFuture.get();
}

void BabylonRenderer::BindRenderTarget(Microsoft::WRL::ComPtr<ID3D11Texture2D> outputTexture)
{
    m_outputTexture = outputTexture;

    D3D11_TEXTURE2D_DESC outputDesc;
    m_outputTexture->GetDesc(&outputDesc);
    m_textureWidth = outputDesc.Width;
    m_textureHeight = outputDesc.Height;
    m_aspectRatio = static_cast<float>(outputDesc.Width) / static_cast<float>(outputDesc.Height);

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
        m_msaaTexture.Reset();
        m_sharedTexture.Reset();
    }

    ID3D11Device* const device = m_babylonDevice.Get();

    // Create the single-sample shared texture on our Babylon device. It is the resolve target and the source
    // for the cross-device copy to the application's texture, so it is created shareable (keyed mutex) and
    // single-sample (a multisampled texture can be neither shared nor CopyResource'd).
    D3D11_TEXTURE2D_DESC sharedDesc{};
    sharedDesc.Width = outputDesc.Width;
    sharedDesc.Height = outputDesc.Height;
    sharedDesc.MipLevels = 1;
    sharedDesc.ArraySize = 1;
    sharedDesc.Format = outputDesc.Format;
    sharedDesc.SampleDesc.Count = 1;
    sharedDesc.SampleDesc.Quality = 0;
    sharedDesc.Usage = D3D11_USAGE_DEFAULT;
    sharedDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    sharedDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    ThrowIfFailed(device->CreateTexture2D(&sharedDesc, nullptr, m_sharedTexture.GetAddressOf()));

    // Create the multisampled texture Babylon renders into (null when the device does not support MSAA for
    // this format, in which case Babylon renders directly into the single-sample shared texture).
    m_msaaTexture = CreateMsaaRenderTexture(device, sharedDesc.Width, sharedDesc.Height, sharedDesc.Format);

    ID3D11Texture2D* const renderTarget = m_msaaTexture ? m_msaaTexture.Get() : m_sharedTexture.Get();
    m_externalTexture = std::make_shared<Babylon::Plugins::ExternalTexture>(renderTarget);

    // Provide the render target's sample count (matching the texture Babylon renders into) and sampling mode
    // to JS so the Babylon RenderTargetTexture - and the depth buffer it allocates - are created to match.
    const int sampleCount = m_msaaTexture ? static_cast<int>(c_msaaSampleCount) : 1;
    const int samplingMode =
        (sharedDesc.MipLevels > 1) ? c_babylonTrilinearSamplingMode : c_babylonBilinearSamplingMode;

    // createRenderTargetTexture is synchronous: it returns the RenderTargetTexture directly.
    DispatchToJsRuntime([this, sampleCount, samplingMode](Napi::Env env, std::promise<void>& done) {
        auto createRenderTargetTexture = env.Global().Get("BI_createRenderTargetTexture").As<Napi::Function>();

        auto renderTargetTexture = createRenderTargetTexture.Call({
            m_sceneContext->Value(),
            m_externalTexture->CreateForJavaScript(env),
            Napi::Value::From(env, m_textureWidth),
            Napi::Value::From(env, m_textureHeight),
            Napi::Value::From(env, sampleCount),
            Napi::Value::From(env, samplingMode),
        }).As<Napi::Object>();

        m_renderTargetTexture = std::make_shared<Napi::ObjectReference>(Napi::Persistent(renderTargetTexture));
        done.set_value();
    });

    // CreateForJavaScript requires one processed frame for the external texture to be registered on the
    // graphics thread before it can be rendered into.
    EndFrame();
    BeginFrame();
}

void BabylonRenderer::ResolveMSAA()
{
    if (!m_msaaTexture)
    {
        // MSAA is unavailable on this device; Babylon rendered directly into the single-sample shared texture.
        return;
    }

    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    m_babylonDevice->GetImmediateContext(context.GetAddressOf());

    D3D11_TEXTURE2D_DESC desc;
    m_sharedTexture->GetDesc(&desc);
    context->ResolveSubresource(m_sharedTexture.Get(), 0, m_msaaTexture.Get(), 0, desc.Format);
    context->Flush();
}

void BabylonRenderer::ApplyCameraTransformJS(Napi::Env env, const ICameraTransform& cam)
{
    constexpr float pi = 3.14159265358979f;

    // The model's screen rectangle within the render target, in pixels. This host frames the model to fill
    // the whole target, so the rectangle is the full texture and there is no viewport margin. A host that
    // renders the model into a sub-rectangle (to leave room for it to overflow while rotating) would pass a
    // smaller rectangle here, and the oversized-viewport math below enlarges the viewport and pads the field
    // of view to match - the same behaviour the ViewportMaterialPlugin applies in the vertex shader.
    const float left = 0.0f;
    const float top = 0.0f;
    const float right = static_cast<float>(m_textureWidth);
    const float bottom = static_cast<float>(m_textureHeight);
    const bool clipped = false;

    float viewportScaleForMargin = 1.0f;
    if (!clipped)
    {
        const float maxMarginX = std::max(left, static_cast<float>(m_textureWidth) - right) / (right - left);
        const float maxMarginY = std::max(top, static_cast<float>(m_textureHeight) - bottom) / (bottom - top);
        viewportScaleForMargin = 1.0f + std::max(maxMarginX, maxMarginY) * 2.0f;
    }

    const float centerX = (left + right) / 2.0f;
    const float centerY = (top + bottom) / 2.0f;
    const float viewportWidth = (right - left) * viewportScaleForMargin;
    const float viewportHeight = (bottom - top) * viewportScaleForMargin;

    const float vpMinX = centerX - viewportWidth / 2.0f;
    const float vpMinY = centerY - viewportHeight / 2.0f;
    const float vpMaxX = centerX + viewportWidth / 2.0f;
    const float vpMaxY = centerY + viewportHeight / 2.0f;

    // Perspective camera (this host does not use an orthographic projection). Pad the vertical field of view
    // to match the oversized viewport so the model keeps the same on-screen size.
    const float fovDegrees = cam.GetFovInDegree();
    const float fovDegreesWithPadding =
        2.0f * std::atan(viewportScaleForMargin * std::tan(fovDegrees / 2.0f * pi / 180.0f)) * 180.0f / pi;
    const float cameraFov = (fovDegreesWithPadding / 360.0f) * 2.0f * pi;

    const float textureWidthF = static_cast<float>(m_textureWidth);
    const float textureHeightF = static_cast<float>(m_textureHeight);

    auto fn = m_sceneContext->Get("applyCameraTransform").As<Napi::Function>();
    fn.Call(m_sceneContext->Value(), {
        Napi::Number::New(env, m_aspectRatio),              // aspectRatio
        Napi::Boolean::New(env, false),                     // orthographic
        Napi::Number::New(env, cameraFov),                  // fovOrOrthographicSize
        Napi::Number::New(env, cam.GetNearClip()),          // minZ
        Napi::Number::New(env, cam.GetFarClip()),           // maxZ
        Napi::Number::New(env, cam.GetPosition().x),        // positionX
        Napi::Number::New(env, cam.GetPosition().y),        // positionY
        Napi::Number::New(env, cam.GetPosition().z),        // positionZ
        Napi::Number::New(env, cam.GetTargetPoint().x),     // targetX
        Napi::Number::New(env, cam.GetTargetPoint().y),     // targetY
        Napi::Number::New(env, cam.GetTargetPoint().z),     // targetZ
        Napi::Number::New(env, cam.GetUpVector().x),        // upVectorX
        Napi::Number::New(env, cam.GetUpVector().y),        // upVectorY
        Napi::Number::New(env, cam.GetUpVector().z),        // upVectorZ
        Napi::Number::New(env, vpMinX / textureWidthF),     // viewportMinX
        Napi::Number::New(env, vpMinY / textureHeightF),    // viewportMinY
        Napi::Number::New(env, vpMaxX / textureWidthF),     // viewportMaxX
        Napi::Number::New(env, vpMaxY / textureHeightF),    // viewportMaxY
        Napi::Number::New(env, 0.0f),                       // scissorLeft (no scissor)
        Napi::Number::New(env, 0.0f),                       // scissorTop
        Napi::Number::New(env, 0.0f),                       // scissorRight
        Napi::Number::New(env, 0.0f),                       // scissorBottom
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

    // Process the GPU frame. Babylon renders into the multisampled texture (when MSAA is available).
    EndFrame();

    // Resolve the multisampled render into the single-sample shared texture while we still hold the mutex,
    // so the copy below reads the resolved result.
    ResolveMSAA();

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
    m_msaaTexture.Reset();
    m_sharedTexture.Reset();

    // Pump a frame so bgfx processes the pending resource cleanup
    // before new resources are created in a subsequent LoadModel call.
    EndFrame();
    BeginFrame();
}

} // namespace IntegrationTest
