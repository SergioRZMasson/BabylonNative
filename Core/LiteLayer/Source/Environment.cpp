#include "EnvironmentInternal.h"

static const char decodeVertex[] = R"(
struct In{@location(0)position:vec3f};
@vertex fn main(input:In)->@builtin(position)vec4f{return vec4f(input.position,1);}
)";

static const char decodeFragment[] = R"(
struct Params{flip:u32};
@group(0)@binding(0)var sourceTexture:texture_2d<f32>;
@group(0)@binding(1)var<uniform>params:Params;
@fragment fn main(@builtin(position)pixel:vec4f)->@location(0)vec4f{
let d=textureDimensions(sourceTexture);let g=vec2u(pixel.xy);
let c=textureLoad(sourceTexture,vec2u(g.x,select(g.y,d.y-1u-g.y,params.flip!=0u)),0);
return vec4f(pow(c.rgb,vec3f(2.2))/max(c.a,1.0/255.0),1);
})";

static void cubeCleanup(bl_Runtime*, L_Record* record)
{
    L_Cube* cube = (L_Cube*)record;
    if (bgfx::isValid(cube->texture))
    {
        bgfx::destroy(cube->texture);
    }
    cube->texture.idx = BL_INVALID_BGFX_HANDLE;
    if (cube->engine)
    {
        l_unpin(&cube->engine->record);
    }
    cube->engine = NULL;
    record->disposed = true;
}

static void hdrTextureCleanup(bl_Runtime*, L_Record* record)
{
    L_Texture* texture = (L_Texture*)record;
    if (texture->handleIndex != BL_INVALID_BGFX_HANDLE)
    {
        bgfx::destroy(bgfx::TextureHandle{texture->handleIndex});
    }
    texture->handleIndex = BL_INVALID_BGFX_HANDLE;
    if (texture->engine)
    {
        l_unpin(&texture->engine->record);
    }
    texture->engine = NULL;
    record->disposed = true;
}

static void environmentCleanup(bl_Runtime* runtime, L_Record* record)
{
    L_Environment* environment = (L_Environment*)record;
    for (size_t i = 0; i < environment->resourceCount; ++i)
    {
        if (environment->frames && bgfx::isValid(environment->frames[i]))
        {
            bgfx::destroy(environment->frames[i]);
        }
        if (environment->inputs && bgfx::isValid(environment->inputs[i]))
        {
            bgfx::destroy(environment->inputs[i]);
        }
    }
    if (bgfx::isValid(environment->triangle))
    {
        bgfx::destroy(environment->triangle);
    }
    l_freeNativePipeline(runtime, &environment->decode);
    L_Record* cube = l_peek(runtime, environment->info.specularCube._id);
    if (cube)
    {
        l_unpin(cube);
        if (!cube->pins && !cube->disposed)
        {
            cubeCleanup(runtime, cube);
        }
    }
    L_Record* brdf = l_peek(runtime, environment->info.brdfLut._id);
    if (brdf)
    {
        l_unpin(brdf);
        if (!brdf->pins && !brdf->disposed)
        {
            hdrTextureCleanup(runtime, brdf);
        }
    }
    l_free(runtime, environment->inputs);
    l_free(runtime, environment->frames);
    environment->inputs = NULL;
    environment->frames = NULL;
    if (environment->engine)
    {
        l_unpin(&environment->engine->record);
    }
    environment->engine = NULL;
    record->disposed = true;
}

bl_Status l_environmentCleanupScene(bl_Runtime* runtime, L_Scene* scene)
{
    L_Environment* environment = scene->environmentHistory;
    while (environment)
    {
        L_Environment* next = environment->next;
        l_unpin(&environment->record);
        environment = next;
    }
    scene->environmentHistory = NULL;
    scene->environment = NULL;
    (void)runtime;
    return BL_OK;
}

bl_Status bl_getTextureCubeInfo(bl_TextureCube handle, bl_TextureCubeInfo* out)
{
    L_GET(handle, L_CUBE, L_Cube, cube);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = cube->info;
    return BL_OK;
}

bl_Status bl_getEnvironmentInfo(bl_EnvironmentTextures handle, bl_EnvironmentInfo* out)
{
    L_GET(handle, L_ENVIRONMENT, L_Environment, environment);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = environment->info;
    return BL_OK;
}

bl_Status bl_disposeEnvironmentTextures(bl_EnvironmentTextures handle)
{
    L_RETIRED(handle, L_ENVIRONMENT, L_Environment, environment);
    if (!environment || environment->record.disposed)
    {
        return BL_OK;
    }
    if (environment->record.pins)
    {
        return BL_BUSY;
    }
    L_TRY(l_retire(environment->engine));
    environmentCleanup(handle._runtime, &environment->record);
    return BL_OK;
}

static bl_Status envImage(bl_Runtime* runtime, bl_Bytes encoded, uint32_t width, uint32_t height,
                          bgfx::TextureHandle* texture)
{
    bl_HostImage image = {};
    bl_Status status;
    L_CALL(runtime, status, runtime->io.decodeImage(runtime->io.userData, encoded, &image));
    size_t rowBytes = (size_t)image.width * 4;
    size_t bytes = 0;
    size_t lastRow = 0;
    uint8_t* tight = NULL;
    if (status == BL_OK)
    {
        if (!image.rgba8 || !image.width || !image.height || (width && image.width != width) ||
            (height && image.height != height) || image.rowStrideBytes < rowBytes ||
            !l_size(rowBytes, image.height, &bytes) || bytes > UINT32_MAX ||
            !l_size(image.rowStrideBytes, image.height ? image.height - 1 : 0, &lastRow) ||
            lastRow > SIZE_MAX - rowBytes ||
            (uintptr_t)image.rgba8 > UINTPTR_MAX - (lastRow + rowBytes) ||
            image.width > UINT16_MAX || image.height > UINT16_MAX)
        {
            status = BL_INVALID_ARGUMENT;
        }
        else
        {
            tight = (uint8_t*)l_alloc(runtime, bytes);
            if (!tight)
            {
                status = BL_OUT_OF_MEMORY;
            }
        }
    }
    if (status == BL_OK)
    {
        for (size_t y = 0; y < image.height; ++y)
        {
            memcpy(tight + y * rowBytes, image.rgba8 + y * image.rowStrideBytes, rowBytes);
        }
        *texture = bgfx::createTexture2D(
            (uint16_t)image.width, (uint16_t)image.height, false, 1, bgfx::TextureFormat::RGBA8,
            BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT,
            bgfx::copy(tight, (uint32_t)bytes));
        if (!bgfx::isValid(*texture))
        {
            status = BL_UNSUPPORTED;
        }
    }
    l_free(runtime, tight);
    l_enterHost(runtime);
    runtime->io.releaseImage(runtime->io.userData, &image);
    l_leaveHost(runtime);
    return status;
}

static void harmonics(L_Environment* environment, const double* raw)
{
    for (unsigned i = 0; i < 27; ++i)
    {
        environment->polynomial[i] = (float)raw[i];
    }
    const float* polynomial = environment->polynomial;
    float* out = environment->harmonics;
    for (unsigned i = 0; i < 3; ++i)
    {
        double x = polynomial[i];
        double y = polynomial[3 + i];
        double z = polynomial[6 + i];
        double xx = polynomial[9 + i];
        double yy = polynomial[12 + i];
        double zz = polynomial[15 + i];
        double yz = polynomial[18 + i];
        double zx = polynomial[21 + i];
        double xy = polynomial[24 + i];
        out[i] = (float)((xx + yy) * 0.3333338747897695 + zz * 0.33333298856284405);
        out[4 + i] = (float)(y * 1.4999984284682104);
        out[8 + i] = (float)(z * 1.4999984284682104);
        out[12 + i] = (float)(x * 1.4999984284682104);
        out[16 + i] = (float)(xy * 3.999982863580422);
        out[20 + i] = (float)(yz * 3.999982863580422);
        out[24 + i] = (float)(zz * 1.3333326611423701 - (xx + yy) * 0.6666653397393608);
        out[28 + i] = (float)(zx * 3.999982863580422);
        out[32 + i] = (float)((xx - yy) * 1.999991431790211);
    }
}

bl_Status bl_loadEnvironment(bl_SceneContext handle, bl_String url,
                             const bl_EnvironmentOptions* options, bl_EnvironmentTextures* out)
{
    L_GET(handle, L_SCENE, L_Scene, scene);
    bl_Runtime* runtime = handle._runtime;
    L_Engine* engine = scene->engine;
    if (l_inFrame() || l_inDispatch() || engine->inFrame)
    {
        return BL_BUSY;
    }
    if (!options || !out || !l_string(url) || !url.length || !l_string(options->brdfUrl) ||
        !options->brdfUrl.length || !l_string(options->groundTextureUrl) ||
        !l_string(options->skyboxUrl) || (unsigned)options->skipGround > BL_BOOL_TRUE ||
        (unsigned)options->skipSkybox > BL_BOOL_TRUE)
    {
        return BL_INVALID_ARGUMENT;
    }
    if (options->skipGround != BL_BOOL_TRUE || options->skipSkybox != BL_BOOL_TRUE ||
        options->groundTextureUrl.length || options->skyboxUrl.length ||
        options->skyboxSize.present)
    {
        return BL_UNSUPPORTED;
    }
    if (!runtime->hasAssets || !runtime->hasCompiler2 || !runtime->hasIO || !runtime->io.readFile ||
        !runtime->io.releaseBuffer || !runtime->io.decodeImage || !runtime->io.releaseImage)
    {
        return BL_UNSUPPORTED;
    }
    if (engine->assetViewCount)
    {
        L_TRY(l_retire(engine));
    }
    bl_HostBuffer input = {};
    bl_Status status;
    L_CALL(runtime, status, runtime->io.readFile(runtime->io.userData, url, &input));
    bl_ParsedEnvironment parsed = {};
    parsed.structSize = sizeof(parsed);
    parsed.version = BL_ASSET_DOCUMENT_VERSION;
    bool invoked = false;
    if (status == BL_OK)
    {
        invoked = true;
        L_CALL(runtime, status,
               runtime->assets.parseEnvironment(runtime->assets.userData,
                                                {input.data, input.byteCount}, &parsed));
    }
    uint32_t mipCount = 0;
    if (status == BL_OK)
    {
        if (parsed.structSize != sizeof(parsed) || parsed.version != 2 || !parsed.hasFileVersion ||
            parsed.fileVersion != 1 || !parsed.width || parsed.width > UINT16_MAX ||
            !l_typedSpan(parsed.faces, parsed.faceCount, alignof(bl_EncodedEnvironmentFace)))
        {
            status = BL_INVALID_ARGUMENT;
        }
        else
        {
            mipCount = (uint32_t)floor(log2(parsed.width)) + 1;
            if (parsed.faceCount != mipCount * 6 || (parsed.width & (parsed.width - 1)) ||
                engine->native.target.viewCount < parsed.faceCount + 2)
            {
                status = BL_UNSUPPORTED;
            }
        }
        for (unsigned i = 0; i < 27 && status == BL_OK; ++i)
        {
            if (!isfinite(parsed.irradiancePolynomial[i]) ||
                !isfinite((float)parsed.irradiancePolynomial[i]))
            {
                status = BL_INVALID_ARGUMENT;
            }
        }
    }
    const bgfx::Caps* caps = bgfx::getCaps();
    if (status == BL_OK &&
        (!(caps->formats[bgfx::TextureFormat::RGBA16F] & BGFX_CAPS_FORMAT_TEXTURE_CUBE) ||
         !(caps->formats[bgfx::TextureFormat::RGBA16F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) ||
         parsed.width > caps->limits.maxTextureSize))
    {
        status = BL_UNSUPPORTED;
    }
    L_Environment* environment = NULL;
    if (status == BL_OK)
    {
        L_Record* record = NULL;
        status = l_record(runtime, sizeof(L_Environment), L_ENVIRONMENT, 30, environmentCleanup,
                          &record);
        if (status == BL_OK)
        {
            environment = (L_Environment*)record;
            environment->engine = engine;
            l_pin(&engine->record);
            environment->triangle.idx = BL_INVALID_BGFX_HANDLE;
            environment->decode.program.idx = BL_INVALID_BGFX_HANDLE;
            environment->resourceCount = parsed.faceCount + 1;
            environment->inputs = (bgfx::TextureHandle*)l_alloc(
                runtime, environment->resourceCount * sizeof(bgfx::TextureHandle));
            environment->frames = (bgfx::FrameBufferHandle*)l_alloc(
                runtime, environment->resourceCount * sizeof(bgfx::FrameBufferHandle));
            if (!environment->inputs || !environment->frames)
            {
                status = BL_OUT_OF_MEMORY;
            }
            for (size_t i = 0; i < environment->resourceCount; ++i)
            {
                if (environment->inputs)
                {
                    environment->inputs[i].idx = BL_INVALID_BGFX_HANDLE;
                }
                if (environment->frames)
                {
                    environment->frames[i].idx = BL_INVALID_BGFX_HANDLE;
                }
            }
        }
    }
    if (status == BL_OK)
    {
        status = l_compileNativePipeline(runtime, engine, {decodeVertex, sizeof(decodeVertex) - 1},
                                         {decodeFragment, sizeof(decodeFragment) - 1}, NULL, 0,
                                         &environment->decode);
    }
    L_Cube* cube = NULL;
    if (status == BL_OK)
    {
        L_Record* record = NULL;
        status = l_record(runtime, sizeof(L_Cube), L_CUBE, 10, cubeCleanup, &record);
        if (status == BL_OK)
        {
            cube = (L_Cube*)record;
            cube->engine = engine;
            l_pin(&engine->record);
            cube->info = {parsed.width, mipCount, BL_TEXTURE_DATA_RGBA16F, false};
            cube->texture = bgfx::createTextureCube((uint16_t)parsed.width, true, 1,
                                                    bgfx::TextureFormat::RGBA16F, BGFX_TEXTURE_RT);
            environment->info.specularCube = {runtime, record->id};
            l_pin(record);
            if (!bgfx::isValid(cube->texture))
            {
                status = BL_UNSUPPORTED;
            }
        }
    }
    for (size_t i = 0; i < parsed.faceCount && status == BL_OK; ++i)
    {
        const bl_EncodedEnvironmentFace* face = parsed.faces + i;
        if (face->mip != i / 6 || (unsigned)face->face != i % 6 ||
            !l_name(face->mimeType, "image/png") ||
            !l_span(face->encoded.data, face->encoded.count))
        {
            status = BL_INVALID_ARGUMENT;
            break;
        }
        uint32_t width = parsed.width >> face->mip;
        status = envImage(runtime, face->encoded, width, width, environment->inputs + i);
        if (status == BL_OK)
        {
            bgfx::Attachment attachment;
            // Automatic generation would overwrite the authored, prefiltered mip faces.
            attachment.init(cube->texture, bgfx::Access::Write, (uint16_t)face->face, 1,
                            (uint8_t)face->mip, 0);
            environment->frames[i] = bgfx::createFrameBuffer(1, &attachment, false);
            if (!bgfx::isValid(environment->frames[i]))
            {
                status = BL_UNSUPPORTED;
            }
        }
    }
    bl_HostBuffer brdfBytes = {};
    bool brdfRead = false;
    L_Texture* brdf = NULL;
    if (status == BL_OK)
    {
        brdfRead = true;
        L_CALL(runtime, status,
               runtime->io.readFile(runtime->io.userData, options->brdfUrl, &brdfBytes));
    }
    bl_HostImage brdfImage = {};
    bool brdfDecoded = false;
    if (status == BL_OK)
    {
        brdfDecoded = true;
        L_CALL(runtime, status,
               runtime->io.decodeImage(runtime->io.userData, {brdfBytes.data, brdfBytes.byteCount},
                                       &brdfImage));
        if (status == BL_OK && (!brdfImage.width || !brdfImage.height))
        {
            status = BL_INVALID_ARGUMENT;
        }
    }
    if (status == BL_OK)
    {
        size_t i = parsed.faceCount;
        status = envImage(runtime, {brdfBytes.data, brdfBytes.byteCount}, brdfImage.width,
                          brdfImage.height, environment->inputs + i);
        L_Record* record = NULL;
        if (status == BL_OK)
        {
            status =
                l_record(runtime, sizeof(L_Texture), L_TEXTURE, 10, hdrTextureCleanup, &record);
        }
        if (status == BL_OK)
        {
            brdf = (L_Texture*)record;
            brdf->engine = engine;
            l_pin(&engine->record);
            brdf->format = BL_TEXTURE_DATA_RGBA16F;
            brdf->info = {brdfImage.width, brdfImage.height, false};
            brdf->handleIndex =
                bgfx::createTexture2D((uint16_t)brdfImage.width, (uint16_t)brdfImage.height, false,
                                      1, bgfx::TextureFormat::RGBA16F, BGFX_TEXTURE_RT)
                    .idx;
            environment->info.brdfLut = {runtime, record->id};
            l_pin(record);
            bgfx::TextureHandle texture = {brdf->handleIndex};
            environment->frames[i] = bgfx::createFrameBuffer(1, &texture, false);
            if (!bgfx::isValid(texture) || !bgfx::isValid(environment->frames[i]))
            {
                status = BL_UNSUPPORTED;
            }
        }
    }
    if (status == BL_OK)
    {
        bgfx::VertexLayout layout;
        layout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
        const float vertices[9] = {-1, -1, 0, 3, -1, 0, -1, 3, 0};
        environment->triangle =
            bgfx::createVertexBuffer(bgfx::copy(vertices, sizeof(vertices)), layout);
        if (!bgfx::isValid(environment->triangle) || environment->decode.textureCount != 1 ||
            environment->decode.blockCount != 1)
        {
            status = BL_SHADER_ERROR;
        }
    }
    if (status == BL_OK)
    {
        harmonics(environment, parsed.irradiancePolynomial);
        environment->info.irradianceSH = {environment->polynomial, 27};
        environment->info.sphericalHarmonics = {environment->harmonics, 36};
        environment->info.lodGenerationScale = 0.8;
        environment->info.lodGenerationOffset = 0;
        environment->info.brdfWidth = brdfImage.width;
        environment->info.brdfHeight = brdfImage.height;
        environment->info.brdfFormat = BL_TEXTURE_DATA_RGBA16F;
        environment->info.cubeSampler = {BL_ADDRESS_CLAMP_TO_EDGE,
                                         BL_ADDRESS_CLAMP_TO_EDGE,
                                         BL_ADDRESS_CLAMP_TO_EDGE,
                                         BL_FILTER_LINEAR,
                                         BL_FILTER_LINEAR,
                                         BL_FILTER_LINEAR,
                                         0,
                                         32,
                                         1};
        environment->info.brdfSampler = environment->info.cubeSampler;
        environment->info.brdfSampler.mipFilter = BL_FILTER_NEAREST;
        for (size_t i = 0; i < environment->resourceCount && status == BL_OK; ++i)
        {
            uint16_t view = (uint16_t)(engine->native.target.firstViewId + i);
            uint16_t width = i == parsed.faceCount ? (uint16_t)brdfImage.width
                                                   : (uint16_t)(parsed.width >> (i / 6));
            uint16_t height = i == parsed.faceCount ? (uint16_t)brdfImage.height : width;
            bgfx::setViewFrameBuffer(view, environment->frames[i]);
            bgfx::setViewRect(view, 0, 0, width, height);
            bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
            bgfx::setVertexBuffer(0, environment->triangle);
            const uint32_t parameters[4] = {i == parsed.faceCount ? 0u : 1u, 0, 0, 0};
            const bl_Bytes blocks[1] = {{(const uint8_t*)parameters, sizeof(parameters)}};
            status = l_bindNativeBlocks(runtime, &environment->decode, blocks, 1);
            if (status == BL_OK)
            {
                bgfx::setTexture(environment->decode.textures[0].stage,
                                 environment->decode.textures[0].uniform, environment->inputs[i]);
                bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
                bgfx::submit(view, environment->decode.program);
            }
        }
    }
    char diagnostic[1024] = {};
    uint32_t diagnosticCode = parsed.diagnostic.sourceErrorCode;
    if (status != BL_OK && l_string(parsed.diagnostic.message))
    {
        size_t length = parsed.diagnostic.message.length < sizeof(diagnostic) - 1
                            ? parsed.diagnostic.message.length
                            : sizeof(diagnostic) - 1;
        while (length && length < parsed.diagnostic.message.length &&
               ((uint8_t)parsed.diagnostic.message.data[length] & 0xc0) == 0x80)
        {
            --length;
        }
        memcpy(diagnostic, parsed.diagnostic.message.data, length);
    }
    if (brdfDecoded)
    {
        l_enterHost(runtime);
        runtime->io.releaseImage(runtime->io.userData, &brdfImage);
        l_leaveHost(runtime);
    }
    if (brdfRead)
    {
        l_enterHost(runtime);
        runtime->io.releaseBuffer(runtime->io.userData, &brdfBytes);
        l_leaveHost(runtime);
    }
    if (invoked)
    {
        l_enterHost(runtime);
        runtime->assets.releaseEnvironment(runtime->assets.userData, &parsed);
        l_leaveHost(runtime);
    }
    l_enterHost(runtime);
    runtime->io.releaseBuffer(runtime->io.userData, &input);
    l_leaveHost(runtime);
    if (status != BL_OK)
    {
        if (environment && l_retire(engine) == BL_OK)
        {
            environmentCleanup(runtime, &environment->record);
        }
        return l_error(runtime, status, __func__,
                       diagnostic[0] ? diagnostic : "Runtime environment load failed",
                       diagnosticCode);
    }
    environment->next = scene->environmentHistory;
    scene->environmentHistory = environment;
    scene->environment = environment;
    l_pin(&environment->record);
    scene->exposure = 0.8;
    scene->contrast = 1.2;
    scene->toneMappingEnabled = true;
    if (environment->resourceCount > engine->assetViewCount)
    {
        engine->assetViewCount = (uint16_t)environment->resourceCount;
    }
    *out = {runtime, environment->record.id};
    return BL_OK;
}
