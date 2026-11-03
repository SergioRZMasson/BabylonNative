#include "LiteInternal.h"

static void textureCleanup(bl_Runtime* runtime, L_Record* p)
{
    L_Texture* t = (L_Texture*)p;
    if (t->mipFrames)
    {
        for (uint8_t mip = 1; mip < t->mipCount; ++mip)
        {
            if (t->mipFrames[mip - 1] != BL_INVALID_BGFX_HANDLE)
            {
                bgfx::destroy(bgfx::FrameBufferHandle{t->mipFrames[mip - 1]});
            }
        }
        l_free(runtime, t->mipFrames);
        t->mipFrames = NULL;
    }
    if (t->handleIndex != BL_INVALID_BGFX_HANDLE)
    {
        bgfx::destroy(bgfx::TextureHandle{t->handleIndex});
    }
    t->handleIndex = BL_INVALID_BGFX_HANDLE;
    if (t->engine)
    {
        l_unpin(&t->engine->record);
    }
    t->engine = NULL;
}

static uint8_t mipLevels(uint32_t width, uint32_t height)
{
    uint8_t count = 1;
    while (width > 1 || height > 1)
    {
        ++count;
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
    }
    return count;
}

static bl_Status pixelSize(bl_Runtime* r, bl_Bytes pixels, uint32_t w, uint32_t h, size_t* bytes)
{
    size_t area;
    if (!w || !h || w > UINT16_MAX || h > UINT16_MAX || !l_size(w, h, &area) ||
        !l_size(area, 4, bytes) || *bytes > UINT32_MAX || !l_span(pixels.data, pixels.count) ||
        pixels.count < *bytes)
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Invalid RGBA8 pixel span");
    }
    return BL_OK;
}

bl_Status l_createAssetPixelTexture(bl_EngineContext h, bl_Bytes pixels, uint32_t w,
                                    uint32_t height, const bl_PixelsTexture2DOptions* o, bool mips,
                                    bl_Texture2D* out)
{
    L_GET(h, L_ENGINE, L_Engine, e);
    bl_Runtime* r = h._runtime;
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    size_t bytes;
    L_TRY(pixelSize(r, pixels, w, height, &bytes));
    bl_PixelsTexture2DOptions options = {};
    if (o)
    {
        options = *o;
    }
    if ((unsigned)options.addressModeU > BL_ADDRESS_MIRROR_REPEAT ||
        (unsigned)options.addressModeV > BL_ADDRESS_MIRROR_REPEAT ||
        (unsigned)options.minFilter > BL_FILTER_LINEAR ||
        (unsigned)options.magFilter > BL_FILTER_LINEAR)
    {
        return BL_INVALID_ARGUMENT;
    }
    uint64_t flags = 0;
    if (options.addressModeU == BL_ADDRESS_CLAMP_TO_EDGE)
    {
        flags |= BGFX_SAMPLER_U_CLAMP;
    }
    if (options.addressModeU == BL_ADDRESS_MIRROR_REPEAT)
    {
        flags |= BGFX_SAMPLER_U_MIRROR;
    }
    if (options.addressModeV == BL_ADDRESS_CLAMP_TO_EDGE)
    {
        flags |= BGFX_SAMPLER_V_CLAMP;
    }
    if (options.addressModeV == BL_ADDRESS_MIRROR_REPEAT)
    {
        flags |= BGFX_SAMPLER_V_MIRROR;
    }
    if (options.minFilter == BL_FILTER_NEAREST)
    {
        flags |= BGFX_SAMPLER_MIN_POINT;
    }
    if (options.magFilter == BL_FILTER_NEAREST)
    {
        flags |= BGFX_SAMPLER_MAG_POINT;
    }
    if (options.srgb)
    {
        flags |= BGFX_TEXTURE_SRGB;
    }
    uint8_t mipCount = mips ? mipLevels(w, height) : 1;
    if (mipCount > 1)
    {
        flags |= BGFX_TEXTURE_RT;
        L_TRY(l_validateTextureMipViews(e, mipCount));
    }
    const bgfx::Caps* caps = bgfx::getCaps();
    if (w > caps->limits.maxTextureSize || height > caps->limits.maxTextureSize ||
        (options.srgb &&
         !(caps->formats[bgfx::TextureFormat::RGBA8] & BGFX_CAPS_FORMAT_TEXTURE_2D_SRGB)) ||
        !bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::RGBA8, flags))
    {
        return L_FAIL(r, BL_UNSUPPORTED, "RGBA8 texture/sampler unsupported by backend");
    }
    L_Record* record = NULL;
    bl_Status status = l_record(r, sizeof(L_Texture), L_TEXTURE, 10, textureCleanup, &record);
    if (status != BL_OK)
    {
        return status;
    }
    L_Texture* t = (L_Texture*)record;
    t->handleIndex = BL_INVALID_BGFX_HANDLE;
    // Initial-data D3D11 textures are immutable on the pinned bgfx backend.
    // Pixel textures expose updates, so allocate writable storage before upload.
    bgfx::TextureHandle handle = bgfx::createTexture2D((uint16_t)w, (uint16_t)height, mipCount > 1,
                                                       1, bgfx::TextureFormat::RGBA8, flags);
    if (!bgfx::isValid(handle))
    {
        t->record.disposed = true;
        return L_FAIL(r, BL_OUT_OF_MEMORY, "Texture GPU allocation failed");
    }
    bgfx::updateTexture2D(handle, 0, 0, 0, 0, (uint16_t)w, (uint16_t)height,
                          bgfx::copy(pixels.data, (uint32_t)bytes));
    t->engine = e;
    t->handleIndex = handle.idx;
    l_pin(&e->record);
    t->info = {w, height, options.srgb};
    t->flags = flags;
    t->mipCount = mipCount;
    if (mipCount > 1)
    {
        status = l_prepareTextureMips(e, t);
        if (status == BL_OK)
        {
            status = l_generateTextureMips(e, t);
        }
        if (status != BL_OK)
        {
            textureCleanup(r, record);
            record->disposed = true;
            return status;
        }
    }
    *out = {r, t->record.id};
    return BL_OK;
}

bl_Status bl_createTexture2DFromPixels(bl_EngineContext engine, bl_Bytes pixels, uint32_t width,
                                       uint32_t height, const bl_PixelsTexture2DOptions* options,
                                       bl_Texture2D* out)
{
    return l_createAssetPixelTexture(engine, pixels, width, height, options, false, out);
}

bl_Status bl_updateTexture2DFromPixels(bl_EngineContext h, bl_Texture2D texture, bl_Bytes pixels,
                                       uint32_t x, uint32_t y, uint32_t w, uint32_t height)
{
    L_GET(h, L_ENGINE, L_Engine, e);
    if (texture._runtime != h._runtime)
    {
        return BL_WRONG_RUNTIME;
    }
    L_GET(texture, L_TEXTURE, L_Texture, t);
    if (t->format != BL_TEXTURE_DATA_RGBA8)
    {
        return BL_UNSUPPORTED;
    }
    if (t->engine != e)
    {
        return BL_WRONG_ENGINE;
    }
    size_t bytes;
    L_TRY(pixelSize(h._runtime, pixels, w, height, &bytes));
    if (x > t->info.width || w > t->info.width - x || y > t->info.height ||
        height > t->info.height - y)
    {
        return BL_INVALID_ARGUMENT;
    }
    if (t->mipCount > 1)
    {
        L_TRY(l_validateTextureMipViews(e, t->mipCount));
        if (!l_inFrame())
        {
            L_TRY(l_retire(e));
        }
        bgfx::updateTexture2D(bgfx::TextureHandle{t->handleIndex}, 0, 0, (uint16_t)x, (uint16_t)y,
                              (uint16_t)w, (uint16_t)height,
                              bgfx::copy(pixels.data, (uint32_t)bytes));
        return l_generateTextureMips(e, t);
    }
    bgfx::updateTexture2D(bgfx::TextureHandle{t->handleIndex}, 0, 0, (uint16_t)x, (uint16_t)y,
                          (uint16_t)w, (uint16_t)height, bgfx::copy(pixels.data, (uint32_t)bytes));
    return BL_OK;
}

bl_Status bl_getTexture2DInfo(bl_Texture2D h, bl_Texture2DInfo* out)
{
    L_GET(h, L_TEXTURE, L_Texture, t);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = t->info;
    return BL_OK;
}

bl_Status bl_disposeTexture2D(bl_Texture2D h)
{
    L_RETIRED(h, L_TEXTURE, L_Texture, t);
    if (!t || t->record.disposed)
    {
        return BL_OK;
    }
    if (t->references)
    {
        return BL_BUSY;
    }
    textureCleanup(h._runtime, &t->record);
    t->record.disposed = true;
    return BL_OK;
}
