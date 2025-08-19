#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#define BabylonRendererDevicePtr          Microsoft::WRL::ComPtr<ID3D11Device>
#define BabylonRendererDeviceContextPtr   Microsoft::WRL::ComPtr<ID3D11DeviceContext>
#define BabylonRendererTexture2DPtr       Microsoft::WRL::ComPtr<ID3D11Texture2D>

namespace IntegrationTestApp
{
    struct ApplicationGraphicsContext
    {
        BabylonRendererDevicePtr Device;
        BabylonRendererDeviceContextPtr Context;
        BabylonRendererTexture2DPtr RenderText;
        const char* BabylonFilePath;
    };
}