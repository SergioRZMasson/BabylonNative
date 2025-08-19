#include "BabylonRendererImpl.h"
#include "BabylonRenderer.h"

namespace IntegrationTestApp
{
    BabylonRenderer::BabylonRenderer(ApplicationGraphicsContext& context)
        : m_impl{std::make_unique<BabylonRendererImpl>(context)}
   {
   }

   void BabylonRenderer::Init()
   {
       m_impl->Init();
   }

   void BabylonRenderer::Render(const Rect& viewport, const Matrix4& sceneTransform, const ICameraTransform& cameraTransform, bool clipped)
   {
       m_impl->Render(viewport, sceneTransform, cameraTransform, clipped);
   }

   void BabylonRenderer::LoadModel3D(std::vector<char> glb, std::vector<char> env)
   {
       m_impl->LoadModel3D(std::move(glb), std::move(env));
   }

   void BabylonRenderer::Release3DModel()
   {
       m_impl->Release3DModel();
   }
}