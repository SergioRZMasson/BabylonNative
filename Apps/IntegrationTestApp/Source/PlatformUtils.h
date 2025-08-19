#pragma once

#include <string>
#include <vector>
#include <RenderTypes.h>

namespace IntegrationTestApp 
{
    void ConsolePrint(std::string message);
   
    std::vector<char> LoadBinaryFile(const char* path);

    bool SaveTextureToPNG(BabylonRendererTexture2DPtr texture, const char* filename);
}