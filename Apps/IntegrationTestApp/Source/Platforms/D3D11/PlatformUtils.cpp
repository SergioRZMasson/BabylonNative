#include <PlatformUtils.h>
#include <Windows.h>
#include <wrl/client.h>
#include <iostream>

// Helper to save a texture to PNG using stb_image_write
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;

namespace IntegrationTestApp
{
    void ConsolePrint(std::string message) 
    {
        OutputDebugStringA(message.c_str());
    }

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

    bool SaveTextureToPNG(Microsoft::WRL::ComPtr<ID3D11Texture2D> texture, const char* filename)
    {
        ComPtr<ID3D11Device> device;
        texture->GetDevice(device.GetAddressOf());

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
        context->CopyResource(stagingTex.Get(), texture.Get());

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
            static_cast<int>(mapped.RowPitch));

        context->Unmap(stagingTex.Get(), 0);
        return result != 0;
    }

    bool CreateApplicationContext(ApplicationGraphicsContext& applicationContext, uint32_t width, uint32_t height)
    {
        // Create D3D11 Device
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;

        if (FAILED(D3D11CreateDevice(
                nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                D3D11_SDK_VERSION, &device, nullptr, &context)))
        {
            std::cerr << "Failed to create D3D11 device\n";
            return false;
        }

        // Create a simple 256x256 RGBA texture and fill with a gradient
        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width = width;
        texDesc.Height = height;
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
            return false;
        }

        applicationContext.Context = context;
        applicationContext.Device = device;
        applicationContext.RenderText = texture;

        return true;
    }
}