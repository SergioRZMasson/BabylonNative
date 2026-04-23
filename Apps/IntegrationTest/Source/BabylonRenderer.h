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

    // Initializes Babylon Native runtime and loads the JS bundle.
    // Width/height are the initial render target dimensions (needed for GPU device creation).
    void Init(const std::string& bundlePath, uint32_t width, uint32_t height);

    // Loads a 3D model and environment from binary data.
    void LoadModel(std::vector<char> modelData, std::vector<char> environmentData);

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
    void WaitForSceneReady();
    void ApplyCameraTransformJS(Napi::Env env, const ICameraTransform& camera);
    void CopyRenderTextureToOutput();

    // Babylon Native objects
    std::unique_ptr<Babylon::Graphics::Device> m_graphicsDevice;
    std::unique_ptr<Babylon::Graphics::DeviceUpdate> m_graphicsDeviceUpdate;
    std::unique_ptr<Babylon::AppRuntime> m_jsRuntime;
    HWND m_bgfxWindow{nullptr}; // Hidden window for bgfx device initialization

    // JS references
    std::shared_ptr<Napi::ObjectReference> m_sceneContext;
    std::shared_ptr<Napi::ObjectReference> m_renderTargetTexture;

    // Texture sharing
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_sharedTexture;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_outputTexture;
    std::shared_ptr<Babylon::Plugins::ExternalTexture> m_externalTexture;

    uint32_t m_textureWidth{0};
    uint32_t m_textureHeight{0};
    float m_aspectRatio{1.0f};
    bool m_sceneReady{false};

    RenderDocCapture m_renderDoc;
};

} // namespace IntegrationTest
