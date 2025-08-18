#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <Babylon/AppRuntime.h>
#include <Babylon/Graphics/Device.h>
#include <Babylon/Plugins/ExternalTexture.h>
#include <string>

#include "BabylonMath.h"

class EngineStats
{
public:
    std::string engineVersion;
    std::string engineName;
};

class BabylonRenderer
{
public:
    // Constructor receives a pointer to ID3D11Device
    BabylonRenderer(ID3D11Device* device, ID3D11DeviceContext* context);

    // Initializes the renderer
    void Init();

    void SetRenderTarget(ID3D11Texture2D* texture);

    // Renders using the provided ID3D11Texture2D
    void Render(const Rect& viewport, const Matrix4& sceneTransform, const ICameraTransform& cameraTransform, bool clipped);

    void LoadModel3D(std::vector<char> glb, std::vector<char> env);
    void DispatchToJsRuntime(std::function<void(Napi::Env, std::promise<void>&)>&& function) const;
    void Release3DModel();

private:
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

    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_deviceContext = nullptr;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_pBabylonRenderTexture = nullptr;
    ID3D11Texture2D* m_poutputRenderTexture = nullptr;
    std::unique_ptr<::Babylon::Graphics::Device> m_pGraphicsDevice;
    std::unique_ptr<::Babylon::Graphics::DeviceUpdate> m_pGraphicsDeviceUpdate;
    std::unique_ptr<::Babylon::AppRuntime> m_pJsRuntime;
    std::shared_ptr<Napi::ObjectReference> m_pContext;
    std::shared_ptr<Napi::ObjectReference> m_pRenderTargetTexture;
    std::shared_ptr<::Babylon::Plugins::ExternalTexture> m_pBabylonExternalTexture;

    EngineStats m_engineStats;
    uint32_t m_textureWidth;
    uint32_t m_textureHeight;
    float m_textureAspectRatio;
};