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
    class BabylonRendererImpl;
   
    class BabylonRenderer
    {
    public:
        // Constructor receives a pointer to ID3D11Device
        BabylonRenderer(ApplicationGraphicsContext& context);
        ~BabylonRenderer() = default;

        // Initializes the renderer
        void Init();

        // Renders using the provided ID3D11Texture2D
        void Render(const Rect& viewport, const Matrix4& sceneTransform, const ICameraTransform& cameraTransform, bool clipped);

        void LoadModel3D(std::vector<char> glb, std::vector<char> env);

        void Release3DModel();

    private:
        std::unique_ptr<BabylonRendererImpl> m_impl;
    };
}