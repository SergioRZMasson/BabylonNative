#pragma once

#include <Babylon/AppRuntime.h>
#include <Babylon/Graphics/Device.h>
#include <Babylon/Plugins/ExternalTexture.h>

#include <string>
#include <memory>

#include "BabylonMath.h"
#include "RenderTypes.h"

namespace IntegrationTestApp
{
    class BabylonRendererImpl
    {
    public:
        // Constructor receives a pointer to ID3D11Device
        BabylonRendererImpl(ApplicationGraphicsContext& context);

        // Initializes the renderer
        void Init();

        // Renders using the provided ID3D11Texture2D
        void Render(const Rect& viewport, const Matrix4& sceneTransform, const ICameraTransform& cameraTransform, bool clipped);

        void LoadModel3D(std::vector<char> glb, std::vector<char> env);

        void Release3DModel();

        void BindRenderTarget();

    private:
        void DispatchToJsRuntime(std::function<void(Napi::Env, std::promise<void>&)>&& function) const;
        void BeginFrame();
        void EndFrame();
        void RenderFrame();
        void RenderJS(Napi::Env);
        void ApplyRootNodeTransform(Napi::Env env, const Matrix4& transform);
        void ApplyCameraTransform(Napi::Env env, const ICameraTransform& cameraTransform,
            float left, float top, float right, float bottom, bool clipped);

        void SetTextureSize(uint32_t width, uint32_t height) noexcept
        {
            m_textureWidth = width;
            m_textureHeight = height;
            m_textureAspectRatio = static_cast<float>(width) / static_cast<float>(height);
        }

        void WaitForSceneReady();

        void CopyRenderTextureToOutput();

        uint32_t TextureWidth() const noexcept { return m_textureWidth; }
        uint32_t TextureHeight() const noexcept { return m_textureHeight; }

        ApplicationGraphicsContext& m_context;

        BabylonRendererTexture2DPtr m_pBabylonRenderTexture = nullptr;
        std::unique_ptr<::Babylon::Graphics::Device> m_pGraphicsDevice;
        std::unique_ptr<::Babylon::Graphics::DeviceUpdate> m_pGraphicsDeviceUpdate;
        std::unique_ptr<::Babylon::AppRuntime> m_pJsRuntime;
        std::shared_ptr<Napi::ObjectReference> m_pContext;
        std::shared_ptr<Napi::ObjectReference> m_pRenderTargetTexture;
        std::shared_ptr<::Babylon::Plugins::ExternalTexture> m_pBabylonExternalTexture;

        uint32_t m_textureWidth;
        uint32_t m_textureHeight;
        float m_textureAspectRatio;
    };
}
