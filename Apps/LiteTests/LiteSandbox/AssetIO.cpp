#include "AssetIO.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using Microsoft::WRL::ComPtr;

static bl_Status readFile(void*, bl_String uri, bl_HostBuffer* out)
{
    try
    {
        if (!uri.length || uri.length > INT_MAX)
        {
            return BL_INVALID_ARGUMENT;
        }
        int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, uri.data,
                                        static_cast<int>(uri.length), nullptr, 0);
        if (!count)
        {
            return BL_INVALID_ARGUMENT;
        }
        std::wstring path(static_cast<size_t>(count), L'\0');
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, uri.data,
                                 static_cast<int>(uri.length), path.data(), count))
        {
            return BL_INVALID_ARGUMENT;
        }
        std::ifstream file(std::filesystem::path(path), std::ios::binary | std::ios::ate);
        if (!file)
        {
            return BL_HOST_ERROR;
        }
        auto end = file.tellg();
        if (end <= 0 || static_cast<uint64_t>(end) > UINT32_MAX)
        {
            return BL_INVALID_ARGUMENT;
        }
        size_t size = static_cast<size_t>(end);
        void* bytes = std::malloc(size);
        if (!bytes)
        {
            return BL_OUT_OF_MEMORY;
        }
        file.seekg(0);
        file.read(static_cast<char*>(bytes), static_cast<std::streamsize>(size));
        if (!file)
        {
            std::free(bytes);
            return BL_HOST_ERROR;
        }
        *out = {static_cast<const uint8_t*>(bytes), size, bytes};
        return BL_OK;
    }
    catch (const std::bad_alloc&)
    {
        return BL_OUT_OF_MEMORY;
    }
    catch (...)
    {
        return BL_HOST_ERROR;
    }
}

static bl_Status decodeImage(void*, bl_Bytes bytes, bl_HostImage* out)
{
    if (!bytes.data || !bytes.count || bytes.count > UINT32_MAX)
    {
        return BL_INVALID_ARGUMENT;
    }
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    HRESULT status = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&factory));
    if (SUCCEEDED(status))
    {
        status = factory->CreateStream(&stream);
    }
    if (SUCCEEDED(status))
    {
        status = stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data),
                                              static_cast<DWORD>(bytes.count));
    }
    if (SUCCEEDED(status))
    {
        status = factory->CreateDecoderFromStream(stream.Get(), nullptr,
                                                  WICDecodeMetadataCacheOnLoad, &decoder);
    }
    if (SUCCEEDED(status))
    {
        GUID container = {};
        status = decoder->GetContainerFormat(&container);
        if (SUCCEEDED(status) && container != GUID_ContainerFormatPng &&
            container != GUID_ContainerFormatJpeg)
        {
            return BL_UNSUPPORTED;
        }
    }
    if (SUCCEEDED(status))
    {
        status = decoder->GetFrame(0, &frame);
    }
    if (SUCCEEDED(status))
    {
        status = factory->CreateFormatConverter(&converter);
    }
    if (SUCCEEDED(status))
    {
        status = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                        WICBitmapDitherTypeNone, nullptr, 0,
                                        WICBitmapPaletteTypeCustom);
    }
    UINT width = 0;
    UINT height = 0;
    if (SUCCEEDED(status))
    {
        status = converter->GetSize(&width, &height);
    }
    if (FAILED(status))
    {
        return status == E_OUTOFMEMORY ? BL_OUT_OF_MEMORY : BL_HOST_ERROR;
    }
    if (!width || !height || uint64_t(width) * height > UINT32_MAX / 4)
    {
        return BL_INVALID_ARGUMENT;
    }
    uint32_t size = width * height * 4;
    uint8_t* rgba = static_cast<uint8_t*>(std::malloc(size));
    if (!rgba)
    {
        return BL_OUT_OF_MEMORY;
    }
    status = converter->CopyPixels(nullptr, width * 4, size, rgba);
    if (FAILED(status))
    {
        std::free(rgba);
        return BL_HOST_ERROR;
    }
    *out = {rgba, width, height, size_t(width) * 4, rgba};
    return BL_OK;
}

static void releaseBuffer(void*, bl_HostBuffer* buffer)
{
    std::free(buffer->allocation);
    *buffer = {};
}

static void releaseImage(void*, bl_HostImage* image)
{
    std::free(image->allocation);
    *image = {};
}

bl_IOService SandboxAssetIO()
{
    return {nullptr, readFile, nullptr, decodeImage, releaseBuffer, releaseImage};
}
