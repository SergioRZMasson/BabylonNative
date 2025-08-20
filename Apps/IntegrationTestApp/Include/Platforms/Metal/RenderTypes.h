#pragma once

#include <MetalKit/MetalKit.h>

#define BabylonRendererDevicePtr        id<MTLDevice>
#define BabylonRendererDeviceContextPtr void*
#define BabylonRendererTexture2DPtr     id<MTLTexture>

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
