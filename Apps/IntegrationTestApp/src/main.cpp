#include <d3d11.h>
#include <wrl/client.h>
#include <wincodec.h>
#include <vector>
#include <iostream>
#include <babylonRenderer.h>
#include "BabylonMath.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;

// Helper to save a texture to PNG using stb_image_write
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

std::vector<char> LoadBinaryFile(const char* path)
{
    std::vector<char> data;
    FILE* file = fopen(path, "rb");
    if (!file)
        return data;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    if (size > 0)
    {
        data.resize(size);
        fseek(file, 0, SEEK_SET);
        fread(data.data(), 1, size, file);
    }
    fclose(file);
    return data;
}

// SaveTextureToPNG: Reads back the texture and saves as PNG using stb_image_write
bool SaveTextureToPNG(ID3D11Device* device, ID3D11Texture2D* texture, const char* filename)
{
    std::remove(filename);

    D3D11_TEXTURE2D_DESC desc = {};
    texture->GetDesc(&desc);

    // Create staging texture for CPU read
    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> stagingTex;
    if (FAILED(device->CreateTexture2D(&stagingDesc, nullptr, &stagingTex)))
        return false;

    ComPtr<ID3D11DeviceContext> context;
    device->GetImmediateContext(&context);
    context->CopyResource(stagingTex.Get(), texture);

    // Map staging texture
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return false;

    // Use mapped data directly; note that stbi_write_png expects tightly packed rows
    int result = stbi_write_png(
        filename,
        desc.Width,
        desc.Height,
        4,
        mapped.pData,
        static_cast<int>(mapped.RowPitch)
    );

    context->Unmap(stagingTex.Get(), 0);
    return result != 0;
}

#define RENDER_WIDTH 1280
#define RENDER_HEIGHT 853

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

    BabylonRenderer renderer(device.Get(), context.Get());
    renderer.Init();

    auto modelData = LoadBinaryFile("./assets/model.glb");
    auto environmentData = LoadBinaryFile("./assets/environment.env");

    renderer.LoadModel3D(modelData, environmentData);
    renderer.SetRenderTarget(texture.Get());

    Matrix4 transform = Matrix4::Identity();
    
    // Set up viewport and camera transform
    Rect viewport(0, 0, RENDER_WIDTH, RENDER_HEIGHT);
    ICameraTransform cameraTransform;
    
    cameraTransform.SetPosition(Vector3(0, 0, 2.26));
    cameraTransform.SetTargetPoint(Vector3(0, 0, 0));
    cameraTransform.SetUpVector(Vector3(0, 1, 0));

    cameraTransform.SetFovInDegree(45.0f);
    cameraTransform.SetNearClip(1.38834774);
    cameraTransform.SetFarClip(3.13771915f);

    renderer.Render(viewport, transform, cameraTransform, false);

    // Save to PNG
    if (SaveTextureToPNG(device.Get(), texture.Get(), "output.png"))
        std::cout << "Saved output.png\n";
    else
        std::cerr << "Failed to save PNG\n";

    return 0;
}