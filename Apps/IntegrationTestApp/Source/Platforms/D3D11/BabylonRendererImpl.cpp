#include <babylonRenderer.h>
#include <BabylonRendererImpl.h>
#include <PlatformUtils.h>
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

#include <BabylonRendererImpl_Shared.h>

namespace IntegrationTestApp
{
    BabylonRendererImpl::BabylonRendererImpl(ApplicationGraphicsContext& context)
        : m_context{context}
        , m_textureWidth{0}
        , m_textureHeight{0}
        , m_textureAspectRatio{1.0f}
    {
    }

    void BabylonRendererImpl::CopyRenderTextureToOutput()
    {
        // Office will not use a GPU texture to get the rendering result from Babylon.
        if (m_context.RenderText == nullptr)
        {
            return;
        }

        Microsoft::WRL::ComPtr<ID3D11Device> factoryDevice;
        m_pBabylonRenderTexture->GetDevice(factoryDevice.GetAddressOf());

        Microsoft::WRL::ComPtr<ID3D11Device> officeProvidedDevice;
        m_context.RenderText->GetDevice(officeProvidedDevice.GetAddressOf());

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
            deviceContext->CopyResource(m_context.RenderText.Get(), tempTexture.Get());
        }
    }

    void BabylonRendererImpl::BindRenderTarget()
    {
        auto factoryDevice = m_pGraphicsDevice->GetPlatformInfo().Device;
        auto factoryD3D11Device = static_cast<ID3D11Device*>(factoryDevice);

        // Create a Render Texture that can be shared by both Office's and Babylon's devices.
        D3D11_TEXTURE2D_DESC desc;
        m_context.RenderText->GetDesc(&desc);
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
}
