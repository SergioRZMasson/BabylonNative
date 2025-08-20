#include <PlatformUtils.h>
#include <iostream>

// Helper to save a texture to PNG using stb_image_write
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"


#include <Metal/Metal.h>


namespace IntegrationTestApp
{
    void ConsolePrint(std::string message)
    {
        NSString *nsMessage = [NSString stringWithUTF8String:message.c_str()];
        NSLog(@"%@", nsMessage);
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
        NSUInteger width = texture.width;
        NSUInteger height = texture.height;

        size_t bytesPerRow = 4 * width;
        size_t imageSize = bytesPerRow * height;
        uint8_t* imageData = (uint8_t*)malloc(imageSize);

        MTLRegion region = MTLRegionMake2D(0, 0, width, height);
        [texture getBytes:imageData
               bytesPerRow:bytesPerRow
                 fromRegion:region
                mipmapLevel:0];
        
        
        stbi_write_png(filename, (int)width, (int)height, 4, imageData, (int)bytesPerRow);
        free(imageData);

        return true;
    }

    bool CreateApplicationContext(ApplicationGraphicsContext& applicationContext, uint32_t width, uint32_t height)
    {
        applicationContext.Device = MTLCreateSystemDefaultDevice();
        applicationContext.Context = nullptr;
        
        MTLTextureDescriptor *desc = [[MTLTextureDescriptor alloc] init];
        desc.textureType = MTLTextureType2D;
        desc.pixelFormat = MTLPixelFormatBGRA8Unorm;
        desc.width = width;
        desc.height = height;
        desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        desc.storageMode = MTLStorageModePrivate;
        
        applicationContext.RenderText = [applicationContext.Device newTextureWithDescriptor:desc];
        return true;
    }
}
