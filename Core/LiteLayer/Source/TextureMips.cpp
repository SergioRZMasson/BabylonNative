#include "NativePipelineInternal.h"

struct L_MipPipeline
{
    L_NativePipeline pipeline;
};

// Original texture/generate-mipmaps.ts, Lite 1.32.0: render mip N-1 into mip N.
static const char* mipVertex = R"WGSL(
struct V{@builtin(position)p:vec4f,@location(0)u:vec2f};
@vertex fn main(@builtin(vertex_index)i:u32)->V{
let p=array<vec2f,3>(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3))[i];
return V(vec4f(p,0,1),p*vec2f(.5,-.5)+.5);}
)WGSL";

static const char* mipFragment = R"WGSL(
@group(0)@binding(0)var t:texture_2d<f32>;@group(0)@binding(1)var s:sampler;
struct V{@builtin(position)p:vec4f,@location(0)u:vec2f};
@fragment fn main(v:V)->@location(0)vec4f{return textureSample(t,s,v.u);}
)WGSL";

bl_Status l_validateTextureMipViews(L_Engine* engine, uint8_t mipCount)
{
    size_t scenes = 0;
    for (L_Scene* scene = engine->scenes; scene; scene = scene->next)
    {
        ++scenes;
    }
    size_t required = mipCount > 1 ? mipCount - 1 : 0;
    if (l_inFrame())
    {
        required += engine->mipViewCursor;
    }
    if (scenes > engine->native.target.viewCount ||
        required > engine->native.target.viewCount - scenes)
    {
        return BL_UNSUPPORTED;
    }
    return BL_OK;
}

bl_Status l_prepareTextureMips(L_Engine* engine, L_Texture* texture)
{
    bl_Runtime* runtime = engine->runtime;
    L_TRY(l_validateTextureMipViews(engine, texture->mipCount));
    if (!engine->mipPipeline)
    {
        L_MipPipeline* state = (L_MipPipeline*)l_alloc(runtime, sizeof(L_MipPipeline));
        if (!state)
        {
            return BL_OUT_OF_MEMORY;
        }
        memset(state, 0, sizeof(*state));
        state->pipeline.program.idx = BL_INVALID_BGFX_HANDLE;
        bl_Status status =
            l_compileNativePipeline(runtime, engine, {mipVertex, strlen(mipVertex)},
                                    {mipFragment, strlen(mipFragment)}, NULL, 0, &state->pipeline);
        if (status == BL_OK &&
            (state->pipeline.textureCount != 1 || state->pipeline.blockCount != 0))
        {
            status = BL_SHADER_ERROR;
        }
        if (status != BL_OK)
        {
            l_freeNativePipeline(runtime, &state->pipeline);
            l_free(runtime, state);
            return status;
        }
        engine->mipPipeline = state;
    }
    size_t count = texture->mipCount - 1;
    texture->mipFrames = (uint16_t*)l_alloc(runtime, count * sizeof(uint16_t));
    if (!texture->mipFrames)
    {
        return BL_OUT_OF_MEMORY;
    }
    for (size_t i = 0; i < count; ++i)
    {
        texture->mipFrames[i] = BL_INVALID_BGFX_HANDLE;
    }
    for (uint8_t mip = 1; mip < texture->mipCount; ++mip)
    {
        bgfx::Attachment attachment;
        attachment.init({texture->handleIndex}, bgfx::Access::Write, 0, 1, mip, 0);
        texture->mipFrames[mip - 1] = bgfx::createFrameBuffer(1, &attachment, false).idx;
        if (texture->mipFrames[mip - 1] == BL_INVALID_BGFX_HANDLE)
        {
            return BL_UNSUPPORTED;
        }
    }
    return BL_OK;
}

bl_Status l_generateTextureMips(L_Engine* engine, L_Texture* texture)
{
    L_TRY(l_validateTextureMipViews(engine, texture->mipCount));
    bool inFrame = l_inFrame();
    if (!inFrame)
    {
        L_TRY(l_retire(engine));
    }
    uint16_t cursor = inFrame ? engine->mipViewCursor : 0;
    L_NativePipeline* pipeline = &engine->mipPipeline->pipeline;
    uint32_t width = texture->info.width;
    uint32_t height = texture->info.height;
    for (uint8_t mip = 1; mip < texture->mipCount; ++mip)
    {
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
        uint16_t view = (uint16_t)(engine->native.target.firstViewId + cursor++);
        bgfx::setViewFrameBuffer(view, bgfx::FrameBufferHandle{texture->mipFrames[mip - 1]});
        bgfx::setViewRect(view, 0, 0, (uint16_t)width, (uint16_t)height);
        bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
        bgfx::setViewClear(view, BGFX_CLEAR_NONE);
        bgfx::setVertexCount(3);
        bgfx::setTexture(pipeline->textures[0].stage, pipeline->textures[0].uniform,
                         bgfx::TextureHandle{texture->handleIndex}, 0, 1, (uint8_t)(mip - 1), 1,
                         BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(view, pipeline->program);
    }
    if (inFrame)
    {
        engine->mipViewCursor = cursor;
    }
    else
    {
        L_TRY(l_retire(engine));
    }
    return BL_OK;
}

void l_freeTextureMipPipeline(bl_Runtime* runtime, L_Engine* engine)
{
    if (engine->mipPipeline)
    {
        l_freeNativePipeline(runtime, &engine->mipPipeline->pipeline);
        l_free(runtime, engine->mipPipeline);
        engine->mipPipeline = NULL;
    }
}
