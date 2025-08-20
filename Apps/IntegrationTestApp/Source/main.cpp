#include <BabylonRendererImpl.h>
#include <BabylonRenderer.h>
#include "BabylonMath.h"
#include "PlatformUtils.h"

#include <vector>
#include <iostream>

#define RENDER_WIDTH 1280
#define RENDER_HEIGHT 853

int main(int argc, char* argv[])
{
    IntegrationTestApp::ApplicationGraphicsContext appContext;

    // Default path as previously in code; overridden if a command-line argument is provided.
    appContext.BabylonFilePath = "http://127.0.0.1:8080/BabylonInterop.bundle.js";

    if (argc > 1 && argv[1] != nullptr && argv[1][0] != '\0')
    {
        appContext.BabylonFilePath = argv[1];
    }

    IntegrationTestApp::CreateApplicationContext(appContext, RENDER_WIDTH, RENDER_HEIGHT);

    IntegrationTestApp::BabylonRenderer renderer(appContext);
    renderer.Init();

    auto modelData = IntegrationTestApp::LoadBinaryFile("./assets/model.glb");
    auto environmentData = IntegrationTestApp::LoadBinaryFile("./assets/environment.env");

    renderer.LoadModel3D(modelData, environmentData);
    renderer.BindRenderTarget();

    IntegrationTestApp::Matrix4 transform = IntegrationTestApp::Matrix4::Identity();

    // Set up viewport and camera transform
    IntegrationTestApp::Rect viewport(0, 0, RENDER_WIDTH, RENDER_HEIGHT);
    IntegrationTestApp::ICameraTransform cameraTransform;

    cameraTransform.SetPosition(IntegrationTestApp::Vector3(0, 0, 2.26f));
    cameraTransform.SetTargetPoint(IntegrationTestApp::Vector3(0, 0, 0));
    cameraTransform.SetUpVector(IntegrationTestApp::Vector3(0, 1, 0));

    cameraTransform.SetFovInDegree(45.0f);
    cameraTransform.SetNearClip(1.38834774f);
    cameraTransform.SetFarClip(3.13771915f);

    renderer.Render(viewport, transform, cameraTransform, false);

    renderer.Release3DModel();

    // Save to PNG
    if (IntegrationTestApp::SaveTextureToPNG(appContext.RenderText, "output.png"))
        std::cout << "Saved output.png\n";
    else
        std::cerr << "Failed to save PNG\n";

    return 0;
}
