#include "AssetIO.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <fstream>
#include <cstdlib>
#include <limits>
#include <string>

namespace
{
    using Microsoft::WRL::ComPtr;

    bl_Status Read(void* user, bl_String uri, bl_HostBuffer* out)
    {
        try
        {
            const std::string name(uri.data, uri.length);
            const auto& directory = static_cast<Scene35Assets*>(user)->directory;
            std::filesystem::path path;
            // Offline resource aliases only; every alias points at licensed RAW source bytes.
            if (name == "https://cdn.jsdelivr.net/gh/KhronosGroup/glTF-Sample-Assets@main/Models/SimpleInstancing/glTF-Binary/SimpleInstancing.glb" ||
                name == "61a3fecd-SimpleInstancing.glb")
                path = directory / "SimpleInstancing.glb";
            else if (name == "https://assets.babylonjs.com/environments/environmentSpecular.env" ||
                     name == "cb5394e5-environmentSpecular.env")
                path = directory / "environmentSpecular.env";
            else if (name == "/brdf-lut.png" || name == "cb14266b-brdf-lut.png")
                path = directory / "brdf-lut.png";
            else
            {
                if (name.size() > INT_MAX) return BL_INVALID_ARGUMENT;
                const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(),
                    static_cast<int>(name.size()), nullptr, 0);
                if (!count) return BL_INVALID_ARGUMENT;
                std::wstring wide(static_cast<size_t>(count), L'\0');
                if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(),
                    static_cast<int>(name.size()), wide.data(), count)) return BL_INVALID_ARGUMENT;
                path = wide;
                if (path.is_relative()) path = directory / path;
            }
            std::ifstream stream(path, std::ios::binary | std::ios::ate);
            if (!stream) return BL_HOST_ERROR;
            const auto size = stream.tellg();
            if (size <= 0 || uint64_t(size) > UINT32_MAX) return BL_INVALID_ARGUMENT;
            void* bytes = std::malloc(static_cast<size_t>(size));
            if (!bytes) return BL_OUT_OF_MEMORY;
            stream.seekg(0);
            stream.read(static_cast<char*>(bytes), static_cast<std::streamsize>(size));
            if (!stream)
            {
                std::free(bytes);
                return BL_HOST_ERROR;
            }
            *out = {static_cast<const uint8_t*>(bytes), static_cast<size_t>(size), bytes};
            return BL_OK;
        }
        catch (const std::bad_alloc&) { return BL_OUT_OF_MEMORY; }
        catch (...) { return BL_HOST_ERROR; }
    }

    bl_Status Decode(void*, bl_Bytes bytes, bl_HostImage* out)
    {
        if (!bytes.data || !bytes.count || bytes.count > UINT32_MAX) return BL_INVALID_ARGUMENT;
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICFormatConverter> converter;
        HRESULT status = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory));
        if (SUCCEEDED(status)) status = factory->CreateStream(&stream);
        if (SUCCEEDED(status)) status = stream->InitializeFromMemory(
            const_cast<BYTE*>(bytes.data), static_cast<DWORD>(bytes.count));
        if (SUCCEEDED(status)) status = factory->CreateDecoderFromStream(
            stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
        if (SUCCEEDED(status)) status = decoder->GetFrame(0, &frame);
        if (SUCCEEDED(status)) status = factory->CreateFormatConverter(&converter);
        if (SUCCEEDED(status)) status = converter->Initialize(frame.Get(),
            GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
            WICBitmapPaletteTypeCustom);
        UINT width{}, height{};
        if (SUCCEEDED(status)) status = converter->GetSize(&width, &height);
        if (FAILED(status)) return BL_HOST_ERROR;
        if (!width || !height || uint64_t(width) * height > UINT32_MAX / 4)
            return BL_INVALID_ARGUMENT;
        const uint32_t size = width * height * 4;
        auto* rgba = static_cast<uint8_t*>(std::malloc(size));
        if (!rgba) return BL_OUT_OF_MEMORY;
        status = converter->CopyPixels(nullptr, width * 4, size, rgba);
        if (FAILED(status))
        {
            std::free(rgba);
            return BL_HOST_ERROR;
        }
        *out = {rgba, width, height, size_t(width) * 4, rgba};
        return BL_OK;
    }
    void ReleaseBuffer(void*, bl_HostBuffer* buffer)
    {
        std::free(buffer->allocation);
        *buffer = {};
    }
    void ReleaseImage(void*, bl_HostImage* image)
    {
        std::free(image->allocation);
        *image = {};
    }
}

bl_IOService Scene35IO(Scene35Assets* assets)
{
    return {assets, Read, nullptr, Decode, ReleaseBuffer, ReleaseImage};
}
