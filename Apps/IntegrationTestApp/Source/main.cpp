
#include <BabylonRendererImpl.h>
#include <BabylonRenderer.h>
#include "BabylonMath.h"
#include "PlatformUtils.h"

#include <wincodec.h>
#include <vector>
#include <iostream>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "windowscodecs.lib")

#define RENDER_WIDTH 1280
#define RENDER_HEIGHT 853

#include <wrl/client.h>
#include <d3d11.h>

using Microsoft::WRL::ComPtr;

int main()
{
    // Create D3D11 Device
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    if (FAILED(D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &device, nullptr, &context)))
    {
        std::cerr << "Failed to create D3D11 device\n";
        return 1;
    }

    // Create a simple 256x256 RGBA texture and fill with a gradient
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = RENDER_WIDTH;
    texDesc.Height = RENDER_HEIGHT;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    texDesc.CPUAccessFlags = 0;
    texDesc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device->CreateTexture2D(&texDesc, nullptr, &texture)))
    {
        std::cerr << "Failed to create texture\n";
        return 1;
    }

    IntegrationTestApp::BabylonRenderer renderer(device.Get(), context.Get());
    renderer.Init();

    auto modelData = IntegrationTestApp::LoadBinaryFile("./assets/model.glb");
    auto environmentData = IntegrationTestApp::LoadBinaryFile("./assets/environment.env");

    renderer.LoadModel3D(modelData, environmentData);
    renderer.SetRenderTarget(texture.Get());

    Matrix4 transform = Matrix4::Identity();
    
    // Set up viewport and camera transform
    Rect viewport(0, 0, RENDER_WIDTH, RENDER_HEIGHT);
    ICameraTransform cameraTransform;
    
    cameraTransform.SetPosition(Vector3(0, 0, 2.26f));
    cameraTransform.SetTargetPoint(Vector3(0, 0, 0));
    cameraTransform.SetUpVector(Vector3(0, 1, 0));

    cameraTransform.SetFovInDegree(45.0f);
    cameraTransform.SetNearClip(1.38834774f);
    cameraTransform.SetFarClip(3.13771915f);

    renderer.Render(viewport, transform, cameraTransform, false);

    renderer.Release3DModel();

    // Save to PNG
    if (IntegrationTestApp::SaveTextureToPNG(texture.Get(), "output.png"))
        std::cout << "Saved output.png\n";
    else
        std::cerr << "Failed to save PNG\n";

    return 0;
}