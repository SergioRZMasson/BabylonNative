#include <PlatformUtils.h>
#include <iostream>

// Helper to save a texture to PNG using stb_image_write
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace IntegrationTestApp
{
    void ConsolePrint(std::string message)
    {
        // TODO: Implement
        std::cout << message.c_str() << std::endl;
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

    bool SaveTextureToPNG(BabylonRendererTexture2DPtr texture, const char* filename)
    {
        // TODO: Implement
        return true;
    }

    bool CreateApplicationContext(ApplicationGraphicsContext& applicationContext, uint32_t width, uint32_t height)
    {
        // TODO: Implement
        return true;
    }
}
