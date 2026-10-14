#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <bgfx/bgfx.h>
#include <fstream>
#include <filesystem>

inline void Scene35GpuState(const std::filesystem::path& path)
{
    using Microsoft::WRL::ComPtr;
    auto* device=static_cast<ID3D11Device*>(bgfx::getInternalData()->context);
    ComPtr<ID3D11DeviceContext> context;
    device->GetImmediateContext(&context);
    ComPtr<ID3D11RenderTargetView> color;
    ComPtr<ID3D11DepthStencilView> depth;
    context->OMGetRenderTargets(1,&color,&depth);
    D3D11_TEXTURE2D_DESC colorDesc{},depthDesc{};
    if(color)
    {
        ComPtr<ID3D11Resource> resource;ComPtr<ID3D11Texture2D> texture;
        color->GetResource(&resource);
        if(SUCCEEDED(resource.As(&texture))) texture->GetDesc(&colorDesc);
    }
    if(depth)
    {
        ComPtr<ID3D11Resource> resource;ComPtr<ID3D11Texture2D> texture;
        depth->GetResource(&resource);
        if(SUCCEEDED(resource.As(&texture))) texture->GetDesc(&depthDesc);
    }
    ComPtr<ID3D11RasterizerState> rasterizer;
    context->RSGetState(&rasterizer);
    D3D11_RASTERIZER_DESC raster{};
    if(rasterizer) rasterizer->GetDesc(&raster);
    std::ofstream file(path);
    file<<"{\"actualColorBound\":"<<(color?"true":"false")<<",\"actualColorFormat\":"<<colorDesc.Format
        <<",\"actualColorWidth\":"<<colorDesc.Width<<",\"actualColorHeight\":"<<colorDesc.Height
        <<",\"actualColorSampleCount\":"<<colorDesc.SampleDesc.Count
        <<",\"actualColorSampleQuality\":"<<colorDesc.SampleDesc.Quality
        <<",\"actualDepthBound\":"<<(depth?"true":"false")<<",\"actualDepthFormat\":"<<depthDesc.Format
        <<",\"actualDepthSampleCount\":"<<depthDesc.SampleDesc.Count
        <<",\"actualRasterMSAAEnabled\":"<<(raster.MultisampleEnable?"true":"false")
        <<",\"actualFrontCounterClockwise\":"<<(raster.FrontCounterClockwise?"true":"false")
        <<",\"actualCullMode\":"<<raster.CullMode<<"}\n";
    if(!file.good()) throw std::runtime_error("Actual GPU state receipt failed");
}
