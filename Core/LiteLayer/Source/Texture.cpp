#include "LiteInternal.h"

static void textureCleanup(bl_Runtime* runtime, L_Record* p)
{
    L_Texture* t = (L_Texture*)p;
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
    l_free(runtime, t->mipPixels);
    t->mipPixels = NULL;
}

static bl_Status mipPixels(bl_Runtime* runtime, const uint8_t* source, uint32_t width,
                           uint32_t height, bool srgb, uint8_t** out, uint32_t* byteCount,
                           uint8_t* mipCount)
{
    uint32_t w = width;
    uint32_t h = height;
    size_t total = 0;
    uint8_t count = 0;
    do
    {
        size_t bytes;
        if (!l_size(w, h, &bytes) || !l_size(bytes, 4, &bytes) || bytes > UINT32_MAX - total)
        {
            return BL_INVALID_ARGUMENT;
        }
        total += bytes;
        ++count;
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    while (w != 1 || h != 1);
    if (width != 1 || height != 1)
    {
        if (total > UINT32_MAX - 4)
        {
            return BL_INVALID_ARGUMENT;
        }
        total += 4;
        ++count;
    }
    uint8_t* pixels = (uint8_t*)l_alloc(runtime, total);
    if (!pixels)
    {
        return BL_OUT_OF_MEMORY;
    }
    size_t baseBytes = (size_t)width * height * 4;
    memcpy(pixels, source, baseBytes);
    uint8_t* previous = pixels;
    uint8_t* next = pixels + baseBytes;
    w = width;
    h = height;
    for (uint8_t level = 1; level < count; ++level)
    {
        uint32_t nextW = w > 1 ? w / 2 : 1;
        uint32_t nextH = h > 1 ? h / 2 : 1;
        for (uint32_t y = 0; y < nextH; ++y)
        {
            uint32_t firstY = y * h / nextH;
            uint32_t lastY = (y + 1) * h / nextH;
            for (uint32_t x = 0; x < nextW; ++x)
            {
                uint32_t firstX = x * w / nextW;
                uint32_t lastX = (x + 1) * w / nextW;
                size_t samples = (size_t)(lastX - firstX) * (lastY - firstY);
                for (unsigned channel = 0; channel < 4; ++channel)
                {
                    double value = 0;
                    for (uint32_t sy = firstY; sy < lastY; ++sy)
                    {
                        for (uint32_t sx = firstX; sx < lastX; ++sx)
                        {
                            double component =
                                previous[((size_t)sy * w + sx) * 4 + channel] / 255.0;
                            if (srgb && channel < 3)
                            {
                                component = component <= 0.04045
                                                ? component / 12.92
                                                : pow((component + 0.055) / 1.055, 2.4);
                            }
                            value += component;
                        }
                    }
                    value /= (double)samples;
                    if (srgb && channel < 3)
                    {
                        value = value <= 0.0031308 ? value * 12.92
                                                   : 1.055 * pow(value, 1.0 / 2.4) - 0.055;
                    }
                    double byte = floor(value * 255 + 0.5);
                    next[((size_t)y * nextW + x) * 4 + channel] = (uint8_t)(byte < 0     ? 0
                                                                            : byte > 255 ? 255
                                                                                         : byte);
                }
            }
        }
        previous = next;
        next += (size_t)nextW * nextH * 4;
        w = nextW;
        h = nextH;
    }
    *out = pixels;
    *byteCount = (uint32_t)total;
    *mipCount = count;
    return BL_OK;
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
    const bgfx::Caps* caps = bgfx::getCaps();
    if (w > caps->limits.maxTextureSize || height > caps->limits.maxTextureSize ||
        (options.srgb &&
         !(caps->formats[bgfx::TextureFormat::RGBA8] & BGFX_CAPS_FORMAT_TEXTURE_2D_SRGB)) ||
        !bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::RGBA8, flags))
    {
        return L_FAIL(r, BL_UNSUPPORTED, "RGBA8 texture/sampler unsupported by backend");
    }
    uint8_t* generated = NULL;
    uint32_t uploadBytes = (uint32_t)bytes;
    uint8_t mipCount = 1;
    if (mips)
    {
        L_TRY(mipPixels(r, pixels.data, w, height, options.srgb, &generated, &uploadBytes,
                        &mipCount));
    }
    L_Record* record = NULL;
    bl_Status status = l_record(r, sizeof(L_Texture), L_TEXTURE, 10, textureCleanup, &record);
    if (status != BL_OK)
    {
        l_free(r, generated);
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
        l_free(r, generated);
        return L_FAIL(r, BL_OUT_OF_MEMORY, "Texture GPU allocation failed");
    }
    uint32_t mipWidth = w;
    uint32_t mipHeight = height;
    size_t offset = 0;
    for (uint8_t mip = 0; mip < mipCount; ++mip)
    {
        uint32_t size = mipWidth * mipHeight * 4;
        bgfx::updateTexture2D(handle, 0, mip, 0, 0, (uint16_t)mipWidth, (uint16_t)mipHeight,
                              bgfx::copy((generated ? generated : pixels.data) + offset, size));
        offset += size;
        mipWidth = mipWidth > 1 ? mipWidth / 2 : 1;
        mipHeight = mipHeight > 1 ? mipHeight / 2 : 1;
    }
    t->engine = e;
    t->handleIndex = handle.idx;
    l_pin(&e->record);
    t->info = {w, height, options.srgb};
    t->flags = flags;
    t->mipCount = mipCount;
    t->mipPixels = generated;
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
        size_t baseBytes = (size_t)t->info.width * t->info.height * 4;
        uint8_t* base = (uint8_t*)l_alloc(h._runtime, baseBytes);
        if (!base)
        {
            return BL_OUT_OF_MEMORY;
        }
        memcpy(base, t->mipPixels, baseBytes);
        for (uint32_t row = 0; row < height; ++row)
        {
            memcpy(base + ((size_t)(y + row) * t->info.width + x) * 4,
                   pixels.data + (size_t)row * w * 4, (size_t)w * 4);
        }
        uint8_t* generated = NULL;
        uint32_t generatedBytes = 0;
        uint8_t count = 0;
        bl_Status status = mipPixels(h._runtime, base, t->info.width, t->info.height, t->info.srgb,
                                     &generated, &generatedBytes, &count);
        l_free(h._runtime, base);
        if (status != BL_OK)
        {
            return status;
        }
        uint32_t mipW = t->info.width;
        uint32_t mipH = t->info.height;
        size_t offset = 0;
        for (uint8_t mip = 0; mip < count; ++mip)
        {
            uint32_t size = mipW * mipH * 4;
            bgfx::updateTexture2D(bgfx::TextureHandle{t->handleIndex}, 0, mip, 0, 0, (uint16_t)mipW,
                                  (uint16_t)mipH, bgfx::copy(generated + offset, size));
            offset += size;
            mipW = mipW > 1 ? mipW / 2 : 1;
            mipH = mipH > 1 ? mipH / 2 : 1;
        }
        l_free(h._runtime, t->mipPixels);
        t->mipPixels = generated;
        return BL_OK;
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
