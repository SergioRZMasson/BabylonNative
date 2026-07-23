#pragma once

#include "MathTypes.h"
#include "RenderDocCapture.h"

#include <Babylon/AppRuntime.h>
#include <Babylon/Graphics/Device.h>
#include <Babylon/Plugins/ExternalTexture.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <functional>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace IntegrationTest
{

struct AnimationInfo
{
    std::string Name;
    float Duration;
};

class BabylonRenderer
{
public:
    BabylonRenderer();
    ~BabylonRenderer();

    // Non-copyable, non-movable
    BabylonRenderer(const BabylonRenderer&) = delete;
    BabylonRenderer& operator=(const BabylonRenderer&) = delete;

    // Initializes Babylon Native runtime, loads the JS bundle, and uploads the environment/IBL map once
    // (shared by every scene). Width/height are the initial render target dimensions (needed for GPU device
    // creation).
    void Init(const std::string& bundlePath, std::vector<char> environmentData, uint32_t width, uint32_t height);

    // Loads a 3D model from binary data. The environment is uploaded once in Init and shared by all scenes.
    void LoadModel(std::vector<char> modelData);

    // Binds the output texture for Babylon to render into.
    void BindRenderTarget(Microsoft::WRL::ComPtr<ID3D11Texture2D> outputTexture);

    // Queries animation information from the loaded model.
    std::vector<AnimationInfo> GetAnimationInfos();

    // Gets the bounding box of the loaded model (rest pose).
    void GetBoundingBox(float center[3], float extents[3]);

    // Renders one frame with the current camera and optional animation.
    void Render(const ICameraTransform& camera, const Matrix4& sceneTransform,
                int animationIndex = -1, float animationProgress = 0.0f);

    // Releases the currently loaded model.
    void ReleaseModel();

    // RenderDoc capture support. Call RequestCapture() to capture the next frame.
    RenderDocCapture& GetRenderDocCapture() { return m_renderDoc; }

private:
    void BeginFrame();
    void EndFrame();
    void DispatchToJsRuntime(std::function<void(Napi::Env, std::promise<void>&)>&& fn);
    void ApplyCameraTransformJS(Napi::Env env, const ICameraTransform& camera);
    void ResolveMSAA();
    void CopyRenderTextureToOutput();

    // The D3D11 device we create and own for Babylon Native. Babylon Native needs a device (or window) up
    // front; a standalone/host app manages its own device(s) separately, so we own a dedicated device here
    // and hand it to Babylon Native via Graphics::Configuration::Device.
    Microsoft::WRL::ComPtr<ID3D11Device> m_babylonDevice;

    // Babylon Native objects
    std::unique_ptr<Babylon::Graphics::Device> m_graphicsDevice;
    std::unique_ptr<Babylon::AppRuntime> m_jsRuntime;

    // JS references
    std::shared_ptr<Napi::ObjectReference> m_sceneContext;
    std::shared_ptr<Napi::ObjectReference> m_renderTargetTexture;

    // Texture sharing. Babylon renders into the multisampled m_msaaTexture (when the device supports MSAA for
    // the format) and we resolve it into the single-sample m_sharedTexture each frame; the shared texture is
    // the one that can be cross-device opened/copied and CPU-read (a multisampled texture can be neither).
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_sharedTexture;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_msaaTexture;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_outputTexture;
    std::shared_ptr<Babylon::Plugins::ExternalTexture> m_externalTexture;

    uint32_t m_textureWidth{0};
    uint32_t m_textureHeight{0};
    float m_aspectRatio{1.0f};

    RenderDocCapture m_renderDoc;
};

} // namespace IntegrationTest
