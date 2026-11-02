#include "AssetIO.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace LiteScenes
{
    namespace
    {
        using Clock = std::chrono::steady_clock;
        using Microsoft::WRL::ComPtr;

        bl_Status Read(void* user, bl_String uri, bl_HostBuffer* output)
        {
            try
            {
                auto& assets = *static_cast<Assets*>(user);
                const auto begin = Clock::now();
                const auto found = assets.paths.find(std::string(uri.data, uri.length));
                if (found == assets.paths.end())
                {
                    return BL_HOST_ERROR;
                }
                std::ifstream file(found->second, std::ios::binary | std::ios::ate);
                if (!file)
                {
                    return BL_HOST_ERROR;
                }
                const auto size = file.tellg();
                if (size <= 0 || uint64_t(size) > UINT32_MAX)
                {
                    return BL_INVALID_ARGUMENT;
                }
                auto* bytes = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(size)));
                if (!bytes)
                {
                    return BL_OUT_OF_MEMORY;
                }
                file.seekg(0);
                file.read(reinterpret_cast<char*>(bytes), static_cast<std::streamsize>(size));
                if (!file)
                {
                    std::free(bytes);
                    return BL_HOST_ERROR;
                }
                *output = {bytes, static_cast<size_t>(size), bytes};
                ++assets.reads;
                assets.bytes += static_cast<size_t>(size);
                assets.readMs +=
                    std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
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

        bl_Status Decode(void* user, bl_Bytes bytes, bl_HostImage* output)
        {
            if (!bytes.data || !bytes.count || bytes.count > UINT32_MAX)
            {
                return BL_INVALID_ARGUMENT;
            }
            const auto begin = Clock::now();
            ComPtr<IWICImagingFactory> factory;
            ComPtr<IWICStream> stream;
            ComPtr<IWICBitmapDecoder> decoder;
            ComPtr<IWICBitmapFrameDecode> frame;
            ComPtr<IWICFormatConverter> converter;
            HRESULT status = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                              CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
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
            UINT width{};
            UINT height{};
            if (SUCCEEDED(status))
            {
                status = converter->GetSize(&width, &height);
            }
            if (FAILED(status))
            {
                return BL_HOST_ERROR;
            }
            if (!width || !height || uint64_t(width) * height > UINT32_MAX / 4)
            {
                return BL_INVALID_ARGUMENT;
            }
            const uint32_t size = width * height * 4;
            auto* rgba = static_cast<uint8_t*>(std::malloc(size));
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
            *output = {rgba, width, height, size_t(width) * 4, rgba};
            auto& assets = *static_cast<Assets*>(user);
            ++assets.decodes;
            assets.decodeMs +=
                std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
            return BL_OK;
        }
    }

    void ReadMap(Assets& assets, const std::filesystem::path& path)
    {
        std::ifstream file(path);
        if (!file)
        {
            throw std::runtime_error("Missing URI map");
        }
        std::string line;
        while (std::getline(file, line))
        {
            const auto tab = line.find('\t');
            if (tab == std::string::npos)
            {
                throw std::runtime_error("Malformed URI map");
            }
            const auto name = line.substr(tab + 1);
            const std::u8string utf8(name.begin(), name.end());
            assets.paths.emplace(line.substr(0, tab), std::filesystem::path(utf8));
        }
    }

    bl_IOService IO(Assets* assets)
    {
        return {assets,
                Read,
                nullptr,
                Decode,
                [](void*, bl_HostBuffer* buffer) {
                    std::free(buffer->allocation);
                    *buffer = {};
                },
                [](void*, bl_HostImage* image) {
                    std::free(image->allocation);
                    *image = {};
                }};
    }
}
