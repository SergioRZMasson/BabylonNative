/*
 * Lite 1.32.0 mesh/thin-instance-gpu-culling.ts + thin-instance-cull-binding.ts
 * (2e064d88, Apache-2.0). Sphere/frustum math, F32 params, atomic count and
 * 64-thread compaction are original. Only instance addressing is interleaved.
 * Approved native slots: NativeEngine VertexBuffer/InstanceRepacker; raw buffer
 * views and dedicated storage follow NativeEngine StorageBuffer (no copied bodies).
 */
#include "ThinCullingInternal.h"
#include "ArcRotateInternal.h"

static const char* resetSource = R"WGSL(
struct Reset { indexCount:u32, baseVertex:u32, p0:u32, p1:u32 };
@group(0) @binding(0) var<storage,read_write> args:array<atomic<u32>>;
@group(0) @binding(1) var<uniform> params:Reset;
@compute @workgroup_size(1)
fn main(){
atomicStore(&args[0],params.indexCount);atomicStore(&args[1],0u);
atomicStore(&args[2],0u);atomicStore(&args[3],params.baseVertex);
atomicStore(&args[4],0u);
}
)WGSL";

static const char* cullBody = R"WGSL(
struct CullParams{planes:array<vec4<f32>,6>,meshWorld:mat4x4<f32>,localSphere:vec4<f32>,count:u32,boundsPad:f32};
@group(0)@binding(0)var<storage,read> srcInstances:array<Instance>;
@group(0)@binding(1)var<storage,read_write> dstInstances:array<Instance>;
@group(0)@binding(2)var<storage,read_write> args:array<atomic<u32>>;
@group(0)@binding(3)var<uniform> params:CullParams;
fn visible(world:mat4x4<f32>)->bool{
let center=(world*vec4<f32>(params.localSphere.xyz,1.0)).xyz;
let sx=length(world[0].xyz);
let sy=length(world[1].xyz);
let sz=length(world[2].xyz);
let radius=params.localSphere.w*max(max(sx,sy),sz)+params.boundsPad+0.0001;
for(var i=0u;i<6u;i++){
let p=params.planes[i];
if(dot(p.xyz,center)+p.w < -radius){return false;}
}
return true;
}
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid:vec3<u32>){
let i=gid.x;
if(i>=params.count){return;}
let world=params.meshWorld*srcInstances[i].world;
if(!visible(world)){return;}
let outIndex=atomicAdd(&args[1],1u);
dstInstances[outIndex]=srcInstances[i];
}
)WGSL";

static void freePipelines(bl_Runtime* r, L_Engine* engine)
{
    L_ThinPipelines* pipelines = engine->thinPipelines;
    if (!pipelines)
    {
        return;
    }
    l_freeComputePipeline(r, &pipelines->reset);
    l_freeComputePipeline(r, &pipelines->cull[0]);
    l_freeComputePipeline(r, &pipelines->cull[1]);
    l_free(r, pipelines);
    engine->thinPipelines = NULL;
    engine->freeThinPipelines = NULL;
}

static void freeState(bl_Runtime* r, L_ThinCullState* state)
{
    if (bgfx::isValid(state->visible))
    {
        bgfx::destroy(state->visible);
    }
    if (bgfx::isValid(state->args))
    {
        bgfx::destroy(state->args);
    }
    l_free(r, state);
}

static void freeCulls(bl_Runtime* r, L_Scene* scene, uint64_t meshId)
{
    L_ThinCullState** link = &scene->thinCulls;
    while (*link)
    {
        L_ThinCullState* state = *link;
        if (!meshId || state->meshId == meshId)
        {
            *link = state->next;
            freeState(r, state);
        }
        else
        {
            link = &state->next;
        }
    }
    if (!scene->thinCulls)
    {
        scene->freeThinCulls = NULL;
    }
}

static bool cullLayout(const L_ComputePipeline* pipeline, bool reset, bool colors)
{
    if (pipeline->workgroupSize[0] != (reset ? 1u : 64u) || pipeline->workgroupSize[1] != 1 ||
        pipeline->workgroupSize[2] != 1 || pipeline->native.blockCount != 1 ||
        pipeline->native.blocks[0].byteSize != (reset ? 16u : 192u) ||
        pipeline->native.blocks[0].group != 0 ||
        pipeline->native.blocks[0].binding != (reset ? 1u : 3u) ||
        pipeline->storageCount != (reset ? 1u : 3u))
    {
        return false;
    }
    uint32_t mask = 0;
    for (size_t i = 0; i < pipeline->storageCount; ++i)
    {
        const bl_ReflectedStorageBuffer* buffer = pipeline->storage + i;
        if (buffer->group != 0 || buffer->binding >= (reset ? 1u : 3u) ||
            buffer->readOnly != (!reset && buffer->binding == 0) ||
            (mask & (1u << buffer->binding)) ||
            buffer->minimumBytes > (buffer->binding == 2 || reset ? 20u
                                    : colors                      ? 80u
                                                                  : 64u))
        {
            return false;
        }
        const char* expected = reset                  ? "args"
                               : buffer->binding == 0 ? "srcInstances"
                               : buffer->binding == 1 ? "dstInstances"
                                                      : "args";
        if (!l_name(buffer->name, expected))
        {
            return false;
        }
        mask |= 1u << buffer->binding;
    }
    return mask == (reset ? 1u : 7u);
}

bl_Status l_prepareThinCulling(bl_Runtime* r, L_Mesh* mesh, bool colors)
{
    if (!(bgfx::getCaps()->supported & BGFX_CAPS_DRAW_INDIRECT) || !r->hasComputeCompiler)
    {
        return L_FAIL(r, BL_UNSUPPORTED, "Thin GPU culling requires compute/indirect services");
    }
    L_Engine* engine = mesh->engine;
    L_ThinPipelines* pipelines = engine->thinPipelines;
    bool created = pipelines == NULL;
    if (created)
    {
        pipelines = (L_ThinPipelines*)l_alloc(r, sizeof(*pipelines));
        if (!pipelines)
        {
            return BL_OUT_OF_MEMORY;
        }
        pipelines->reset.native.program.idx = BL_INVALID_BGFX_HANDLE;
        pipelines->cull[0].native.program.idx = BL_INVALID_BGFX_HANDLE;
        pipelines->cull[1].native.program.idx = BL_INVALID_BGFX_HANDLE;
        bl_Status status = l_compileComputePipeline(r, engine, {resetSource, strlen(resetSource)},
                                                    {"main", 4}, &pipelines->reset);
        if (status != BL_OK || !cullLayout(&pipelines->reset, true, false))
        {
            l_freeComputePipeline(r, &pipelines->reset);
            l_free(r, pipelines);
            return status != BL_OK ? status : BL_SHADER_ERROR;
        }
        engine->thinPipelines = pipelines;
        engine->freeThinPipelines = freePipelines;
    }
    unsigned variant = colors ? 1 : 0;
    if (!pipelines->ready[variant])
    {
        const char* record = colors ? "struct Instance{world:mat4x4<f32>,color:vec4<f32>};\n"
                                    : "struct Instance{world:mat4x4<f32>};\n";
        L_Text source = {};
        bool ok = l_appendText(r, &source, {record, strlen(record)}) &&
                  l_appendText(r, &source, {cullBody, strlen(cullBody)});
        bl_Status status = ok ? l_compileComputePipeline(r, engine, {source.data, source.length},
                                                         {"main", 4}, pipelines->cull + variant)
                              : BL_OUT_OF_MEMORY;
        l_free(r, source.data);
        if (status != BL_OK || !cullLayout(pipelines->cull + variant, false, colors))
        {
            l_freeComputePipeline(r, pipelines->cull + variant);
            if (created)
            {
                freePipelines(r, engine);
            }
            return status != BL_OK ? status : BL_SHADER_ERROR;
        }
        pipelines->ready[variant] = true;
    }
    L_Geometry* g = &mesh->geometry;
    if (!g->dedicatedIndices && g->indexCapacity)
    {
        // Dedicated storage has a zero base index, matching indexed-indirect args.
        L_TRY(l_validateStorageUploadViews(engine, 1));
        bgfx::DynamicIndexBufferHandle buffer = bgfx::createDynamicIndexBuffer(
            (uint32_t)g->indexCapacity, BGFX_BUFFER_INDEX32 | BGFX_BUFFER_COMPUTE_READ_WRITE);
        if (!bgfx::isValid(buffer))
        {
            return BL_OUT_OF_MEMORY;
        }
        bgfx::BufferRegion destination;
        destination.init(buffer, 0, (uint32_t)(g->indexCapacity * sizeof(uint32_t)));
        L_StorageUpload upload;
        bl_Status status = l_prepareStorageUpload(
            &destination, g->gpuIndices ? g->gpuIndices : g->indices, &upload);
        if (status == BL_OK)
        {
            status = l_submitStorageUploads(engine, &upload, 1);
        }
        if (status != BL_OK)
        {
            bgfx::destroy(buffer);
            return status;
        }
        bgfx::destroy(g->indexBuffer);
        g->indexBuffer = buffer;
        g->dedicatedIndices = true;
        l_publishGeometryGpu(g);
    }
    return BL_OK;
}

static void plane(float* out, unsigned offset, double x, double y, double z, double w)
{
    double length = l_hypot3(x, y, z);
    double inverse = 1 / length;
    out[offset] = (float)(x * inverse);
    out[offset + 1] = (float)(y * inverse);
    out[offset + 2] = (float)(z * inverse);
    out[offset + 3] = (float)(w * inverse);
}

static bl_Status cullState(bl_Runtime* r, L_Scene* scene, L_Mesh* mesh, L_ThinCullState** out)
{
    L_ThinCullState* reusable = NULL;
    L_ThinCullState* state = scene->thinCulls;
    while (state)
    {
        if (state->meshId == mesh->node.record.id)
        {
            if (state->sequence == scene->drawSequence)
            {
                break;
            }
            if (state->frame != scene->engine->frameSerial)
            {
                reusable = state;
            }
        }
        state = state->next;
    }
    if (!state)
    {
        state = reusable;
    }
    bool created = state == NULL;
    if (created)
    {
        state = (L_ThinCullState*)l_alloc(r, sizeof(*state));
        if (!state)
        {
            return BL_OUT_OF_MEMORY;
        }
        state->visible.idx = state->args.idx = BL_INVALID_BGFX_HANDLE;
    }
    uint16_t stride = mesh->instanceGpuStride;
    if (state->capacity < mesh->instanceCount || state->stride != stride)
    {
        bgfx::VertexLayout layout;
        layout.begin().skip((uint8_t)stride).end();
        bgfx::DynamicVertexBufferHandle visible = bgfx::createDynamicVertexBuffer(
            (uint32_t)mesh->instanceCapacity, layout, BGFX_BUFFER_COMPUTE_READ_WRITE);
        bgfx::IndirectBufferHandle args = bgfx::createIndirectBuffer(1);
        if (!bgfx::isValid(visible) || !bgfx::isValid(args))
        {
            if (bgfx::isValid(visible))
            {
                bgfx::destroy(visible);
            }
            if (bgfx::isValid(args))
            {
                bgfx::destroy(args);
            }
            if (created)
            {
                l_free(r, state);
            }
            return BL_OUT_OF_MEMORY;
        }
        if (bgfx::isValid(state->visible))
        {
            bgfx::destroy(state->visible);
        }
        if (bgfx::isValid(state->args))
        {
            bgfx::destroy(state->args);
        }
        state->visible = visible;
        state->args = args;
        state->capacity = mesh->instanceCapacity;
        state->stride = stride;
    }
    if (created)
    {
        state->next = scene->thinCulls;
        scene->thinCulls = state;
        scene->freeThinCulls = freeCulls;
    }
    state->meshId = mesh->node.record.id;
    state->sequence = scene->drawSequence;
    state->frame = scene->engine->frameSerial;
    *out = state;
    return BL_OK;
}

bl_Status l_prepareThinDraw(bl_Runtime* r, L_Scene* scene, L_Mesh* mesh, const bl_Mat4* view,
                            const bl_Mat4* projection, uint16_t viewId, bool colors, bool excluded,
                            L_ThinDraw* out)
{
    L_ThinDraw draw = {{BL_INVALID_BGFX_HANDLE}, {BL_INVALID_BGFX_HANDLE}};
    if (!mesh->hasInstances || !mesh->instanceCount)
    {
        *out = draw;
        return BL_OK;
    }
    L_TRY(l_prepareThinBuffer(r, mesh, colors));
    draw.instances.idx = mesh->instanceBufferIndex;
    if (!mesh->instanceCulling || excluded)
    {
        *out = draw;
        return BL_OK;
    }
    L_TRY(l_prepareThinCulling(r, mesh, colors));
    L_ThinCullState* state;
    L_TRY(cullState(r, scene, mesh, &state));
    L_ThinPipelines* pipelines = scene->engine->thinPipelines;
    const uint32_t reset[4] = {(uint32_t)mesh->geometry.indexCount, 0, 0, 0};
    bl_Bytes block = {(const uint8_t*)reset, sizeof(reset)};
    L_TRY(l_bindNativeBlocks(r, &pipelines->reset.native, &block, 1));
    bgfx::setBuffer((uint8_t)pipelines->reset.storage[0].nativeStage, state->args,
                    bgfx::Access::ReadWrite);
    bgfx::dispatch(viewId, pipelines->reset.native.program, 1, 1, 1);
    if (pipelines->viewFrame != scene->engine->frameSerial ||
        pipelines->viewScene != scene->record.id)
    {
        bl_Mat4 absoluteView;
        const bl_Mat4* cullView = view;
        if (scene->engine->floatingOrigin)
        {
            // writeCullParams uses the camera's absolute VP and absolute mesh world,
            // not the camera-relative matrices used only by the rendering uniforms.
            L_Node* camera;
            L_TRY(l_cameraFamily(scene->activeCamera, &camera));
            if (camera->record.kind == L_ARC_CAMERA)
            {
                l_arcView(camera, &absoluteView);
            }
            else
            {
                l_inverse(&camera->world, &absoluteView, scene->engine->highPrecision);
            }
            cullView = &absoluteView;
        }
        l_multiply(projection, cullView, &pipelines->viewProjection, scene->engine->highPrecision);
        pipelines->viewFrame = scene->engine->frameSerial;
        pipelines->viewScene = scene->record.id;
    }
    const double* m = pipelines->viewProjection.values;
    float params[48] = {};
    plane(params, 0, m[3] + m[0], m[7] + m[4], m[11] + m[8], m[15] + m[12]);
    plane(params, 4, m[3] - m[0], m[7] - m[4], m[11] - m[8], m[15] - m[12]);
    plane(params, 8, m[3] + m[1], m[7] + m[5], m[11] + m[9], m[15] + m[13]);
    plane(params, 12, m[3] - m[1], m[7] - m[5], m[11] - m[9], m[15] - m[13]);
    plane(params, 16, m[2], m[6], m[10], m[14]);
    plane(params, 20, m[3] - m[2], m[7] - m[6], m[11] - m[10], m[15] - m[14]);
    for (unsigned i = 0; i < 16; ++i)
    {
        params[24 + i] = (float)mesh->node.world.values[i];
    }
    const bl_Vec3* low = &mesh->geometry.minimum;
    const bl_Vec3* high = &mesh->geometry.maximum;
    double x = low->x * .5 + high->x * .5;
    double y = low->y * .5 + high->y * .5;
    double z = low->z * .5 + high->z * .5;
    params[40] = (float)x;
    params[41] = (float)y;
    params[42] = (float)z;
    x = high->x - x;
    y = high->y - y;
    z = high->z - z;
    params[43] = (float)l_hypot3(x, y, z);
    uint32_t count = (uint32_t)mesh->instanceCount;
    memcpy(params + 44, &count, sizeof(count));
    L_ComputePipeline* pipeline = pipelines->cull + (colors ? 1 : 0);
    block = {(const uint8_t*)params, sizeof(params)};
    L_TRY(l_bindNativeBlocks(r, &pipeline->native, &block, 1));
    for (size_t i = 0; i < pipeline->storageCount; ++i)
    {
        const bl_ReflectedStorageBuffer* binding = pipeline->storage + i;
        if (binding->binding == 0)
        {
            bgfx::setBuffer((uint8_t)binding->nativeStage, draw.instances, bgfx::Access::Read);
        }
        else if (binding->binding == 1)
        {
            bgfx::setBuffer((uint8_t)binding->nativeStage, state->visible, bgfx::Access::ReadWrite);
        }
        else
        {
            bgfx::setBuffer((uint8_t)binding->nativeStage, state->args, bgfx::Access::ReadWrite);
        }
    }
    bgfx::dispatch(viewId, pipeline->native.program, (count + 63) / 64, 1, 1);
    draw.instances = state->visible;
    draw.indirect = state->args;
    *out = draw;
    return BL_OK;
}
