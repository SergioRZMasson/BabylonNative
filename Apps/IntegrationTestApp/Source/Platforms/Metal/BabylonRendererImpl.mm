#include <BabylonRenderer.h>
#include <BabylonRendererImpl.h>
#include <PlatformUtils.h>
#include <GraphicsDebug.h>

#include <Babylon/ScriptLoader.h>
#include <Babylon/Polyfills/Console.h>
#include <Babylon/Polyfills/Window.h>
#include <Babylon/Polyfills/XMLHttpRequest.h>
#include <Babylon/Plugins/NativeEngine.h>
#include <Babylon/Plugins/ExternalTexture.h>

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
    }

    void BabylonRendererImpl::BindRenderTarget()
    {
        auto factoryDevice = m_pGraphicsDevice->GetPlatformInfo().Device;

        // TODO: Create render target texture.
        uint32_t width = 0;
        uint32_t height = 0;

        m_pBabylonExternalTexture = std::make_shared<::Babylon::Plugins::ExternalTexture>(m_pBabylonRenderTexture);

        std::promise<void> renderFrame;
        std::promise<void> done;

        SetTextureSize(width, height);

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

