#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <stdexcept>
#include <vector>

// Independent driver mip generation, not the Lite pipeline or a CPU box-filter surrogate.
static std::vector<std::vector<uint8_t>> referenceMips(
    const uint8_t* pixels, uint32_t width, uint32_t height, bool srgb)
{
    using Microsoft::WRL::ComPtr;
    auto check = [](HRESULT result)
    {
        if (FAILED(result))
        {
            throw std::runtime_error("Independent D3D11 mip reference failed");
        }
    };
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &device, nullptr, &context));
    D3D11_TEXTURE2D_DESC descriptor{};
    descriptor.Width = width;
    descriptor.Height = height;
    descriptor.ArraySize = 1;
    descriptor.Format = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    descriptor.SampleDesc.Count = 1;
    descriptor.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    descriptor.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    ComPtr<ID3D11Texture2D> texture;
    check(device->CreateTexture2D(&descriptor, nullptr, &texture));
    context->UpdateSubresource(texture.Get(), 0, nullptr, pixels, width * 4, 0);
    ComPtr<ID3D11ShaderResourceView> view;
    check(device->CreateShaderResourceView(texture.Get(), nullptr, &view));
    context->GenerateMips(view.Get());
    texture->GetDesc(&descriptor);
    const auto levels = descriptor.MipLevels;
    descriptor.BindFlags = 0;
    descriptor.MiscFlags = 0;
    descriptor.Usage = D3D11_USAGE_STAGING;
    descriptor.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    check(device->CreateTexture2D(&descriptor, nullptr, &staging));
    context->CopyResource(staging.Get(), texture.Get());
    std::vector<std::vector<uint8_t>> result;
    for (uint32_t mip = 0; mip < levels; ++mip)
    {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(staging.Get(), mip, D3D11_MAP_READ, 0, &mapped));
        auto& bytes = result.emplace_back(size_t(width) * height * 4);
        for (uint32_t row = 0; row < height; ++row)
        {
            memcpy(bytes.data() + size_t(row) * width * 4,
                static_cast<const uint8_t*>(mapped.pData) + size_t(row) * mapped.RowPitch,
                size_t(width) * 4);
        }
        context->Unmap(staging.Get(), mip);
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
    }
    return result;
}
