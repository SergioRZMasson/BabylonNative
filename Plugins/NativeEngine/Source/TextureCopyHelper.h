#pragma once

#include "NativeDataStream.h"
#include "PerFrameValue.h"
#include "ShaderCompiler.h"
#include "VertexArray.h"

#include <Babylon/JsRuntime.h>
#include <Babylon/JsRuntimeScheduler.h>

#include <Babylon/Graphics/DeviceContext.h>
#include <Babylon/Graphics/BgfxCallback.h>
#include <Babylon/Graphics/FrameBuffer.h>
#include <Babylon/Graphics/Texture.h>

#include <napi/napi.h>

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bimg/bimg.h>
#include <bx/allocator.h>

#include <gsl/gsl>

#include <arcana/threading/cancellation.h>
#include <unordered_map>

namespace Babylon
{
    class TextureCopyHelper
    {
    public:
        static void Initialize();
        static void CopyTexture(bgfx::Encoder* encoder, Babylon::Graphics::Texture* dst, Babylon::Graphics::Texture* src, bool flipY);

    private:
        TextureCopyHelper() = delete;
        ~TextureCopyHelper() = delete;
        TextureCopyHelper(const TextureCopyHelper&) = delete;
        TextureCopyHelper& operator=(const TextureCopyHelper&) = delete;
    };
}