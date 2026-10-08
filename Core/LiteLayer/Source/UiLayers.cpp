#include "UiInternal.h"

static void surfaceInit(U_Surface* surface)
{
    memset(surface, 0, sizeof(*surface));
    surface->color = BGFX_INVALID_HANDLE;
    surface->depth = BGFX_INVALID_HANDLE;
    surface->framebuffer = BGFX_INVALID_HANDLE;
}

static void surfaceDestroy(U_Context* c, U_Surface* surface)
{
    if (bgfx::isValid(surface->framebuffer))
    {
        bgfx::destroy(surface->framebuffer);
    }
    if (bgfx::isValid(surface->color))
    {
        bgfx::destroy(surface->color);
        ++c->stats.textureReleaseCount;
        --c->stats.liveTextureCount;
    }
    if (bgfx::isValid(surface->depth))
    {
        bgfx::destroy(surface->depth);
        ++c->stats.textureReleaseCount;
        --c->stats.liveTextureCount;
    }
    surfaceInit(surface);
}

void u_layersInit(U_Context* c)
{
    for (unsigned i = 0; i < U_MAX_LAYERS; ++i)
    {
        surfaceInit(c->layers + i);
    }
    for (unsigned i = 0; i < 2; ++i)
    {
        surfaceInit(c->scratch + i);
    }
}

void u_layersDestroy(U_Context* c)
{
    for (unsigned i = 0; i < U_MAX_LAYERS; ++i)
    {
        surfaceDestroy(c, c->layers + i);
    }
    for (unsigned i = 0; i < 2; ++i)
    {
        surfaceDestroy(c, c->scratch + i);
    }
}

void u_layersResize(U_Context* c)
{
    u_layersDestroy(c);
    if (c->layerQuad)
    {
        u_release(c, c->layerQuad, U_GEOMETRY);
        c->layerQuad = 0;
    }
}

static bool surfaceCreate(U_Context* c, U_Surface* surface)
{
    uint32_t width = c->options.target.width;
    uint32_t height = c->options.target.height;
    if (bgfx::isValid(surface->framebuffer) && surface->width == width && surface->height == height)
    {
        return true;
    }
    if (!width || !height || width > bgfx::getCaps()->limits.maxTextureSize ||
        height > bgfx::getCaps()->limits.maxTextureSize || width > UINT16_MAX ||
        height > UINT16_MAX || c->resourceSerial == UINT64_MAX)
    {
        u_resourceFailure(c, BL_UNSUPPORTED, "UI layer dimensions exceed backend capacity");
        return false;
    }
    surfaceDestroy(c, surface);
    surface->color = bgfx::createTexture2D(
        (uint16_t)width, (uint16_t)height, false, 1, bgfx::TextureFormat::RGBA8,
        BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    if (bgfx::isValid(surface->color))
    {
        ++c->stats.textureCreateCount;
        ++c->stats.liveTextureCount;
    }
    surface->depth = bgfx::createTexture2D((uint16_t)width, (uint16_t)height, false, 1,
                                           bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT_WRITE_ONLY);
    if (bgfx::isValid(surface->depth))
    {
        ++c->stats.textureCreateCount;
        ++c->stats.liveTextureCount;
    }
    if (bgfx::isValid(surface->color) && bgfx::isValid(surface->depth))
    {
        const bgfx::TextureHandle attachments[] = {surface->color, surface->depth};
        surface->framebuffer = bgfx::createFrameBuffer(2, attachments, false);
    }
    if (!bgfx::isValid(surface->framebuffer))
    {
        surfaceDestroy(c, surface);
        u_resourceFailure(c, BL_OUT_OF_MEMORY, "UI offscreen layer allocation failed");
        return false;
    }
    surface->width = width;
    surface->height = height;
    surface->id = ++c->resourceSerial;
    return true;
}

static bool surfaceView(U_Context* c, bgfx::FrameBufferHandle framebuffer, uint16_t clearFlags,
                        bool replayClip)
{
    uint16_t view;
    if (!u_allocateView(c, &view))
    {
        return false;
    }
    c->currentView = view;
    bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
    bgfx::setViewFrameBuffer(view, framebuffer);
    bgfx::setViewRect(view, 0, 0, (uint16_t)c->options.target.width,
                      (uint16_t)c->options.target.height);
    bgfx::setViewClear(view, clearFlags, UINT32_C(0), 0.0f, UINT8_C(0));
    bgfx::touch(view);
    c->submitted = true;
    if (replayClip && c->clipEnabled)
    {
        u_replayClip(c);
    }
    return c->pending == BL_OK;
}

bool u_layerView(U_Context* c, unsigned layer, uint16_t clearFlags, bool replayClip)
{
    bgfx::FrameBufferHandle framebuffer = BGFX_INVALID_HANDLE;
    if (layer)
    {
        if (layer > U_MAX_LAYERS || !surfaceCreate(c, c->layers + layer - 1))
        {
            return false;
        }
        framebuffer = c->layers[layer - 1].framebuffer;
    }
    else if (c->options.target.kind == BL_TARGET_BGFX_FRAMEBUFFER)
    {
        framebuffer.idx = c->options.target.framebufferIndex;
    }
    return surfaceView(c, framebuffer, clearFlags, replayClip);
}

uint64_t u_pushLayer(U_Context* c)
{
    if (bgfx::getCaps()->originBottomLeft)
    {
        u_resourceFailure(c, BL_UNSUPPORTED, "UI layers require top-left render-target sampling");
        return 0;
    }
    if (c->whiteDifference)
    {
        u_resourceFailure(c, BL_UNSUPPORTED, "White-difference contexts cannot use layers");
        return 0;
    }
    if (c->layerDepth == U_MAX_LAYERS)
    {
        u_resourceFailure(c, BL_UNSUPPORTED, "UI layer stack exceeds backend capacity");
        return 0;
    }
    unsigned next = c->layerDepth + 1;
    if (!u_layerView(c, next, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL, true))
    {
        return 0;
    }
    c->layerDepth = next;
    return c->layers[next - 1].id;
}

void u_popLayer(U_Context* c)
{
    if (!c->layerDepth)
    {
        u_resourceFailure(c, BL_INVALID_ARGUMENT, "UI layer stack is empty");
        return;
    }
    --c->layerDepth;
    u_layerView(c, c->layerDepth, BGFX_CLEAR_STENCIL, true);
}

static unsigned layerIndex(U_Context* c, uint64_t id)
{
    if (!id)
    {
        return 0;
    }
    for (unsigned i = 0; i < U_MAX_LAYERS; ++i)
    {
        if (c->layers[i].id == id && bgfx::isValid(c->layers[i].framebuffer))
        {
            return i + 1;
        }
    }
    u_resourceFailure(c, BL_INVALID_HANDLE, "Stale UI layer identity");
    return UINT_MAX;
}

static bool quadReady(U_Context* c)
{
    if (c->layerQuad)
    {
        return true;
    }
    float width = (float)c->options.target.width;
    float height = (float)c->options.target.height;
    const float vertices[] = {0,     0,      1, 1, 1, 1, 0, 0, width, 0,      1, 1, 1, 1, 1, 0,
                              width, height, 1, 1, 1, 1, 1, 1, 0,     height, 1, 1, 1, 1, 0, 1};
    const uint32_t indices[] = {0, 1, 2, 0, 2, 3};
    c->layerQuad = u_geometry(c, vertices, 4, indices, 6);
    return c->layerQuad != 0;
}

static void identityTransform(U_Context* c)
{
    memset(c->transform, 0, sizeof(c->transform));
    c->transform[0] = 1;
    c->transform[5] = 1;
    c->transform[10] = 1;
    c->transform[15] = 1;
}

void u_composite(U_Context* c, uint64_t sourceId, uint64_t destinationId, bool replace,
                 const uint64_t* filters, size_t filterCount)
{
    if (c->pending != BL_OK)
    {
        return;
    }
    if (c->whiteDifference || filterCount > 1 || !l_span(filters, filterCount))
    {
        u_resourceFailure(c, BL_UNSUPPORTED, "UI composition supports one Gaussian filter");
        return;
    }
    unsigned source = layerIndex(c, sourceId);
    unsigned destination = layerIndex(c, destinationId);
    if (!source || source == UINT_MAX || destination == UINT_MAX || !quadReady(c))
    {
        u_resourceFailure(c, BL_UNSUPPORTED, "UI composition source must be an offscreen layer");
        return;
    }
    float sigma = 0;
    if (filterCount)
    {
        U_Resource* filter = u_resource(c, filters[0], U_FILTER);
        if (!filter)
        {
            return;
        }
        sigma = filter->gradient[0][0];
    }
    bool clipEnabled = c->clipEnabled;
    bool scissorEnabled = c->scissorEnabled;
    float transform[16];
    memcpy(transform, c->transform, sizeof(transform));
    identityTransform(c);
    bgfx::TextureHandle texture = c->layers[source - 1].color;
    if (filterCount || source == destination)
    {
        c->clipEnabled = false;
        c->scissorEnabled = false;
        for (unsigned pass = 0; pass < 2; ++pass)
        {
            U_Surface* scratch = c->scratch + pass;
            if (!surfaceCreate(c, scratch) ||
                !surfaceView(c, scratch->framebuffer, BGFX_CLEAR_COLOR | BGFX_CLEAR_STENCIL, false))
            {
                break;
            }
            const float parameters[] = {pass == 0 ? 1.0f / (float)c->options.target.width : 0.0f,
                                        pass == 1 ? 1.0f / (float)c->options.target.height : 0.0f,
                                        sigma, ceilf(3 * sigma)};
            u_drawRaw(c, c->layerQuad, texture, parameters, true);
            texture = scratch->color;
        }
        c->clipEnabled = clipEnabled;
        c->scissorEnabled = scissorEnabled;
    }
    if (c->pending == BL_OK && u_layerView(c, destination, BGFX_CLEAR_STENCIL, true))
    {
        identityTransform(c);
        u_drawRaw(c, c->layerQuad, texture, NULL, replace);
    }
    memcpy(c->transform, transform, sizeof(transform));
}

uint64_t u_saveLayer(U_Context* c)
{
    if (c->whiteDifference || !c->layerDepth)
    {
        u_resourceFailure(c, BL_UNSUPPORTED,
                          "Saving the base or white-difference layer is unsupported");
        return 0;
    }
    if (!bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_BLIT_DST))
    {
        u_resourceFailure(c, BL_UNSUPPORTED, "UI saved layers require real texture blit support");
        return 0;
    }
    int32_t left = c->scissorEnabled && c->scissor[0] > 0 ? c->scissor[0] : 0;
    int32_t top = c->scissorEnabled && c->scissor[1] > 0 ? c->scissor[1] : 0;
    int32_t right = (int32_t)c->options.target.width;
    int32_t bottom = (int32_t)c->options.target.height;
    if (c->scissorEnabled)
    {
        right = c->scissor[2] < right ? c->scissor[2] : right;
        bottom = c->scissor[3] < bottom ? c->scissor[3] : bottom;
    }
    if (right <= left || bottom <= top)
    {
        u_resourceFailure(c, BL_INVALID_ARGUMENT, "UI saved layer has an empty scissor region");
        return 0;
    }
    uint32_t width = (uint32_t)(right - left);
    uint32_t height = (uint32_t)(bottom - top);
    uint64_t id = u_textureStorage(
        c, width, height, BGFX_TEXTURE_BLIT_DST | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    if (!id)
    {
        return 0;
    }
    U_Resource* saved = u_resource(c, id, U_TEXTURE);
    uint16_t view;
    if (!saved || !u_allocateView(c, &view))
    {
        if (saved)
        {
            u_release(c, id, U_TEXTURE);
        }
        return 0;
    }
    bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
    bgfx::TextureRegion source;
    source.init(c->layers[c->layerDepth - 1].color);
    source.x = (uint16_t)left;
    source.y = (uint16_t)top;
    source.width = (uint16_t)width;
    source.height = (uint16_t)height;
    bgfx::TextureRegion destination;
    destination.init(saved->texture);
    destination.width = (uint16_t)width;
    destination.height = (uint16_t)height;
    bgfx::blit(view, destination, source);
    c->submitted = true;
    return id;
}
