#include "UiInternal.h"
#include <stdio.h>

static const char* uniformNames[] = {
    "projection", "transform", "translation", "gradient0", "gradient1", "stop0",  "stop1",
    "stop2",      "stop3",     "stop4",       "stop5",     "stop6",     "stop7",  "color0",
    "color1",     "color2",    "color3",      "color4",    "color5",    "color6", "color7"};

static const char vertexSource[] = R"(
struct UiUniforms {
    projection: mat4x4f, transform: mat4x4f, translation: vec4f,
    gradient0: vec4f, gradient1: vec4f,
    stop0: vec4f, stop1: vec4f, stop2: vec4f, stop3: vec4f,
    stop4: vec4f, stop5: vec4f, stop6: vec4f, stop7: vec4f,
    color0: vec4f, color1: vec4f, color2: vec4f, color3: vec4f,
    color4: vec4f, color5: vec4f, color6: vec4f, color7: vec4f
};
@group(1) @binding(0) var<uniform> ui: UiUniforms;
struct VertexInput {
    @location(0) position: vec2f,
    @location(1) color: vec4f,
    @location(2) uv: vec2f
};
struct Output {
    @builtin(position) position: vec4f,
    @location(0) color: vec4f,
    @location(1) uv: vec2f
};
@vertex fn mainVertex(input: VertexInput) -> Output {
    var output: Output;
    output.position = ui.projection * ui.transform *
        vec4f(input.position + ui.translation.xy, 0.0, 1.0);
    output.color = input.color;
    output.uv = input.uv;
    return output;
}
)";

static const char plainFragment[] = R"(
@fragment fn mainFragment(@location(0) color: vec4f) -> @location(0) vec4f {
    return color;
}
)";

static const char texturedFragment[] = R"(
@group(1) @binding(1) var image: texture_2d<f32>;
@group(1) @binding(2) var imageSampler: sampler;
@fragment fn mainFragment(@location(0) color: vec4f, @location(1) uv: vec2f)
    -> @location(0) vec4f {
    return color * textureSampleLevel(image, imageSampler, uv, 0.0);
}
)";

static const char gradientFragment[] = R"(
struct UiUniforms {
    projection: mat4x4f, transform: mat4x4f, translation: vec4f,
    gradient0: vec4f, gradient1: vec4f,
    stop0: vec4f, stop1: vec4f, stop2: vec4f, stop3: vec4f,
    stop4: vec4f, stop5: vec4f, stop6: vec4f, stop7: vec4f,
    color0: vec4f, color1: vec4f, color2: vec4f, color3: vec4f,
    color4: vec4f, color5: vec4f, color6: vec4f, color7: vec4f
};
@group(1) @binding(0) var<uniform> ui: UiUniforms;
@fragment fn mainFragment(@location(0) color: vec4f, @location(1) uv: vec2f)
    -> @location(0) vec4f {
    var t: f32;
    if (ui.gradient1.z > 0.5) {
        t = length((uv - ui.gradient0.xy) / ui.gradient0.zw);
    } else {
        let direction = ui.gradient0.zw - ui.gradient0.xy;
        t = dot(uv - ui.gradient0.xy, direction) / dot(direction, direction);
    }
    let stops = array<f32, 8>(ui.stop0.x, ui.stop1.x, ui.stop2.x, ui.stop3.x,
        ui.stop4.x, ui.stop5.x, ui.stop6.x, ui.stop7.x);
    let colors = array<vec4f, 8>(ui.color0, ui.color1, ui.color2, ui.color3,
        ui.color4, ui.color5, ui.color6, ui.color7);
    let count = u32(ui.gradient1.x);
    if (ui.gradient1.y > 0.5) {
        let interval = stops[count - 1u] - stops[0];
        if (interval > 0.000001) {
            t = stops[0] + (t - stops[0] - floor((t - stops[0]) / interval) * interval);
        }
    }
    var result = colors[0];
    for (var i = 1u; i < count; i = i + 1u) {
        if (t >= stops[i - 1u]) {
            let interval = stops[i] - stops[i - 1u];
            let factor = select(1.0, clamp((t - stops[i - 1u]) /
                max(interval, 0.000001), 0.0, 1.0), interval > 0.000001);
            result = mix(colors[i - 1u], colors[i], factor);
        }
    }
    return color * result;
}
)";

static const char blurFragment[] = R"(
struct UiUniforms {
    projection: mat4x4f, transform: mat4x4f, translation: vec4f,
    gradient0: vec4f, gradient1: vec4f,
    stop0: vec4f, stop1: vec4f, stop2: vec4f, stop3: vec4f,
    stop4: vec4f, stop5: vec4f, stop6: vec4f, stop7: vec4f,
    color0: vec4f, color1: vec4f, color2: vec4f, color3: vec4f,
    color4: vec4f, color5: vec4f, color6: vec4f, color7: vec4f
};
@group(1) @binding(0) var<uniform> ui: UiUniforms;
@group(1) @binding(1) var image: texture_2d<f32>;
@group(1) @binding(2) var imageSampler: sampler;
@fragment fn mainFragment(@location(0) color: vec4f, @location(1) uv: vec2f)
    -> @location(0) vec4f {
    let sigma = ui.gradient0.z;
    if (sigma < 0.00001) {
        return color * textureSample(image, imageSampler, uv);
    }
    let step = ui.gradient0.xy;
    let radius = u32(ui.gradient0.w);
    var sum = textureSampleLevel(image, imageSampler, uv, 0.0);
    var normalization = 1.0;
    for (var i = 1u; i <= radius; i = i + 2u) {
        let first = f32(i);
        let second = f32(i + 1u);
        let w0 = exp(-0.5 * first * first / (sigma * sigma));
        var w1 = 0.0;
        if (i + 1u <= radius) {
            w1 = exp(-0.5 * second * second / (sigma * sigma));
        }
        let weight = w0 + w1;
        let offset = (first * w0 + second * w1) / weight;
        sum = sum + weight * (textureSampleLevel(image, imageSampler, uv + step * offset, 0.0) +
            textureSampleLevel(image, imageSampler, uv - step * offset, 0.0));
        normalization = normalization + 2.0 * weight;
    }
    return color * sum / normalization;
}
)";

void u_pending(U_Context* c, bl_Status status, const char* message)
{
    if (c->recording && c->renderFailure == BL_OK)
    {
        c->renderFailure = status;
        snprintf(c->renderMessage, sizeof(c->renderMessage), "%s", message);
    }
    if (c->pending == BL_OK)
    {
        c->pending = status;
        snprintf(c->pendingMessage, sizeof(c->pendingMessage), "%s", message);
    }
}

bl_Status u_finish(U_Context* c, const char* operation)
{
    bl_Status status = c->pending;
    c->pending = BL_OK;
    if (status != BL_OK)
    {
        return l_error(c->runtime, status, operation, c->pendingMessage);
    }

    return BL_OK;
}

void u_resourceFailure(U_Context* c, bl_Status status, const char* message)
{
    u_pending(c, status, message);
    if (c->renderFailure == BL_OK)
    {
        c->renderFailure = status;
        snprintf(c->renderMessage, sizeof(c->renderMessage), "%s", message);
    }
}

static void programDestroy(bl_Runtime* r, U_Program* p)
{
    if (bgfx::isValid(p->handle))
    {
        bgfx::destroy(p->handle);
    }
    if (bgfx::isValid(p->sampler))
    {
        bgfx::destroy(p->sampler);
    }
    for (size_t i = 0; i < p->uniformCount; ++i)
    {
        U_Uniform* u = p->uniforms + i;
        if (bgfx::isValid(u->handle))
        {
            bgfx::destroy(u->handle);
        }
        l_free(r, (void*)u->name.data);
        l_free(r, u->bytes);
    }
    memset(p, 0, sizeof(*p));
    p->handle = BGFX_INVALID_HANDLE;
    p->sampler = BGFX_INVALID_HANDLE;
}

static bl_Status reflectProgram(U_Context* c, U_Program* p, const bl_ShaderCompileResult* result,
                                bool textured)
{
    bl_Runtime* r = c->runtime;
    if (result->attributeCount > 3 || result->uniformCount > 24 || result->uniformBlockCount > 1 ||
        result->textureCount != (size_t)textured ||
        !l_typedSpan(result->attributes, result->attributeCount, alignof(bl_ReflectedAttribute)) ||
        !l_typedSpan(result->uniforms, result->uniformCount, alignof(bl_ReflectedUniform)) ||
        !l_typedSpan(result->uniformBlocks, result->uniformBlockCount,
                     alignof(bl_ReflectedUniformBlock)) ||
        !l_typedSpan(result->textures, result->textureCount, alignof(bl_ReflectedTexture)))
    {
        return BL_SHADER_ERROR;
    }
    unsigned attributes = 0;
    const bl_VertexSemantic semantics[] = {BL_ATTRIBUTE_POSITION, BL_ATTRIBUTE_COLOR,
                                           BL_ATTRIBUTE_UV};
    const uint8_t components[] = {2, 4, 2};
    const char* names[] = {"position", "color", "uv"};
    for (size_t i = 0; i < result->attributeCount; ++i)
    {
        const bl_ReflectedAttribute* a = result->attributes + i;
        if (!l_string(a->name) || a->location >= 3 || !l_name(a->name, names[a->location]) ||
            semantics[a->location] != a->semantic || components[a->location] != a->components ||
            (attributes & (1u << a->location)))
        {
            return BL_SHADER_ERROR;
        }
        attributes |= 1u << a->location;
    }
    if (!(attributes & 1))
    {
        return BL_SHADER_ERROR;
    }
    for (size_t i = 0; i < result->uniformBlockCount; ++i)
    {
        const bl_ReflectedUniformBlock* b = result->uniformBlocks + i;
        if (!l_name(b->name, "ui") || b->group != 1 || b->binding != 0 || b->byteSize != 432)
        {
            return BL_SHADER_ERROR;
        }
    }
    unsigned sources = 0;
    for (size_t i = 0; i < result->uniformCount; ++i)
    {
        const bl_ReflectedUniform* v = result->uniforms + i;
        unsigned source = 21;
        for (unsigned j = 0; j < 21; ++j)
        {
            if (l_name(v->name, uniformNames[j]))
            {
                source = j;
                break;
            }
        }
        uint32_t expectedBytes = source < 2 ? 64 : 16;
        uint32_t expectedOffset = source < 2 ? source * 64 : 128 + (source - 2) * 16;
        if (source >= 21 || (sources & (1u << source)) || !l_string(v->nativeName) ||
            !v->nativeName.length || v->nativeName.length > 255 || v->blockIndex != 0 ||
            v->byteOffset != expectedOffset || v->byteSize != expectedBytes ||
            v->type != (source < 2 ? BL_UNIFORM_MAT4 : BL_UNIFORM_VEC4) || !v->stages ||
            (v->stages & ~UINT32_C(3)) || (unsigned)v->nativeType > BL_NATIVE_UNIFORM_MAT4 ||
            !v->nativeCount)
        {
            return BL_SHADER_ERROR;
        }
        sources |= 1u << source;
        size_t destination = p->uniformCount;
        for (size_t j = 0; j < p->uniformCount; ++j)
        {
            if (l_equal(v->nativeName, p->uniforms[j].name))
            {
                destination = j;
                break;
            }
        }
        if (destination == p->uniformCount)
        {
            U_Uniform* u = p->uniforms + p->uniformCount++;
            u->handle = BGFX_INVALID_HANDLE;
            u->name = l_copyString(r, v->nativeName);
            u->type = v->nativeType;
            u->count = v->nativeCount;
            u->byteCount = (v->nativeType == BL_NATIVE_UNIFORM_MAT4   ? 64
                            : v->nativeType == BL_NATIVE_UNIFORM_MAT3 ? 48
                                                                      : 16) *
                           (size_t)v->nativeCount;
            if (u->byteCount > 432)
            {
                return BL_SHADER_ERROR;
            }
            u->bytes = (uint8_t*)l_alloc(r, u->byteCount);
            if (!u->name.data || !u->bytes)
            {
                return BL_OUT_OF_MEMORY;
            }
            bgfx::UniformType::Enum type =
                v->nativeType == BL_NATIVE_UNIFORM_MAT4   ? bgfx::UniformType::Mat4
                : v->nativeType == BL_NATIVE_UNIFORM_MAT3 ? bgfx::UniformType::Mat3
                                                          : bgfx::UniformType::Vec4;
            u->handle = bgfx::createUniform(u->name.data, type, u->count);
            if (!bgfx::isValid(u->handle))
            {
                return BL_OUT_OF_MEMORY;
            }
        }
        U_Uniform* u = p->uniforms + destination;
        if (u->type != v->nativeType || u->count != v->nativeCount || (v->nativeByteOffset & 3) ||
            v->nativeByteOffset > u->byteCount || v->byteSize > u->byteCount - v->nativeByteOffset)
        {
            return BL_SHADER_ERROR;
        }
        for (size_t j = 0; j < p->mapCount; ++j)
        {
            const U_UniformMap* m = p->maps + j;
            if (m->destination == destination && m->offset < v->nativeByteOffset + v->byteSize &&
                v->nativeByteOffset < m->offset + m->bytes)
            {
                return BL_SHADER_ERROR;
            }
        }
        p->maps[p->mapCount++] = {source, (unsigned)destination, v->nativeByteOffset, v->byteSize};
    }
    if ((sources & 7) != 7)
    {
        return BL_SHADER_ERROR;
    }
    if (textured)
    {
        const bl_ReflectedTexture* t = result->textures;
        if (!l_name(t->name, "image") || !l_name(t->samplerName, "imageSampler") ||
            !l_string(t->nativeName) || !t->nativeName.length || t->nativeName.length > 255 ||
            t->textureGroup != 1 || t->textureBinding != 1 || t->samplerGroup != 1 ||
            t->samplerBinding != 2 || t->stages != BL_STAGE_FRAGMENT ||
            t->nativeTextureStage >= bgfx::getCaps()->limits.maxTextureSamplers)
        {
            return BL_SHADER_ERROR;
        }
        bl_String name = l_copyString(r, t->nativeName);
        if (!name.data)
        {
            return BL_OUT_OF_MEMORY;
        }
        p->sampler = bgfx::createUniform(name.data, bgfx::UniformType::Sampler);
        p->textureStage = t->nativeTextureStage;
        l_free(r, (void*)name.data);
        if (!bgfx::isValid(p->sampler))
        {
            return BL_OUT_OF_MEMORY;
        }
    }
    return BL_OK;
}

static bl_Status programCreate(U_Context* c, U_Program* p, const char* fragment, bool textured)
{
    bl_Runtime* r = c->runtime;
    const bgfx::Caps* caps = bgfx::getCaps();
    bl_ShaderCompileRequest request = {};
    request.contractVersion = BL_CONTRACT_VERSION;
    request.backend = c->engine->backend;
    request.bgfxShaderBinaryVersion = 12;
    request.vertex = {
        BL_STAGE_VERTEX, {vertexSource, sizeof(vertexSource) - 1}, {"mainVertex", 10}};
    request.fragment = {BL_STAGE_FRAGMENT, {fragment, strlen(fragment)}, {"mainFragment", 12}};
    request.homogeneousDepth = caps->homogeneousDepth;
    request.originBottomLeft = caps->originBottomLeft;
    request.label = {"LiteLayer retained UI", 21};
    bl_ShaderCompileResult result = {};
    bl_Status status;
    L_CALL(r, status, r->compiler.compile(r->compiler.userData, &request, &result));
    if (status == BL_OK)
    {
        if (!result.vertexContainer.data || result.vertexContainer.count < 8 ||
            result.vertexContainer.count > UINT32_MAX || !result.fragmentContainer.data ||
            result.fragmentContainer.count < 8 || result.fragmentContainer.count > UINT32_MAX ||
            memcmp(result.vertexContainer.data, "VSH", 3) || result.vertexContainer.data[3] != 12 ||
            memcmp(result.fragmentContainer.data, "FSH", 3) ||
            result.fragmentContainer.data[3] != 12)
        {
            status = BL_SHADER_ERROR;
        }
        else
        {
            status = reflectProgram(c, p, &result, textured);
        }
    }
    if (status == BL_OK)
    {
        bgfx::ShaderHandle vertex = bgfx::createShader(
            bgfx::copy(result.vertexContainer.data, (uint32_t)result.vertexContainer.count));
        bgfx::ShaderHandle pixel = bgfx::createShader(
            bgfx::copy(result.fragmentContainer.data, (uint32_t)result.fragmentContainer.count));
        if (bgfx::isValid(vertex) && bgfx::isValid(pixel))
        {
            p->handle = bgfx::createProgram(vertex, pixel, true);
        }
        else
        {
            if (bgfx::isValid(vertex))
            {
                bgfx::destroy(vertex);
            }
            if (bgfx::isValid(pixel))
            {
                bgfx::destroy(pixel);
            }
        }
        if (!bgfx::isValid(p->handle))
        {
            status = BL_SHADER_ERROR;
        }
    }
    if (status != BL_OK)
    {
        if (l_string(result.diagnostics) && result.diagnostics.length)
        {
            char message[512];
            snprintf(message, sizeof(message), "%.*s",
                     (int)(result.diagnostics.length > 500 ? 500 : result.diagnostics.length),
                     result.diagnostics.data);
            u_pending(c, status, message);
        }
        else
        {
            u_pending(c, status, "Invalid UI shader containers or reflection");
        }
    }
    l_enterHost(r);
    r->compiler.release(r->compiler.userData, &result);
    l_leaveHost(r);
    return status;
}

bl_Status u_gpuCreate(U_Context* c)
{
    for (size_t i = 0; i < U_PROGRAM_COUNT; ++i)
    {
        c->programs[i].handle = BGFX_INVALID_HANDLE;
        c->programs[i].sampler = BGFX_INVALID_HANDLE;
    }
    L_TRY(programCreate(c, c->programs, plainFragment, false));
    L_TRY(programCreate(c, c->programs + 1, texturedFragment, true));
    L_TRY(programCreate(c, c->programs + 2, gradientFragment, false));
    return programCreate(c, c->programs + 3, blurFragment, true);
}

static void resourceDestroy(U_Resource* p)
{
    if (p->kind == U_GEOMETRY)
    {
        if (bgfx::isValid(p->vertices))
        {
            bgfx::destroy(p->vertices);
        }
        if (bgfx::isValid(p->indices))
        {
            bgfx::destroy(p->indices);
        }
    }
    else if (p->kind == U_TEXTURE && bgfx::isValid(p->texture))
    {
        bgfx::destroy(p->texture);
    }
}

void u_gpuDestroy(U_Context* c)
{
    while (c->resources)
    {
        U_Resource* p = c->resources;
        c->resources = p->next;
        resourceDestroy(p);
        l_free(c->runtime, p);
    }
    u_layersDestroy(c);
    for (size_t i = 0; i < U_PROGRAM_COUNT; ++i)
    {
        programDestroy(c->runtime, c->programs + i);
    }
}

void u_collect(U_Context* c)
{
    U_Resource** link = &c->resources;
    while (*link)
    {
        U_Resource* p = *link;
        if (p->retired)
        {
            *link = p->next;
            resourceDestroy(p);
            l_free(c->runtime, p);
        }
        else
        {
            link = &p->next;
        }
    }
}

U_Resource* u_resource(U_Context* c, uint64_t handle, unsigned kind)
{
    for (U_Resource* p = c->resources; p; p = p->next)
    {
        if (p->handle == handle && p->kind == kind &&
            (!p->retired || (c->replayingClip && kind == U_GEOMETRY)))
        {
            return p;
        }
    }
    u_resourceFailure(c, BL_INVALID_HANDLE, "Stale or wrong-kind RmlUI GPU resource handle");
    return NULL;
}

static U_Resource* resourceCreate(U_Context* c, unsigned kind)
{
    if (c->resourceSerial == UINT64_MAX)
    {
        u_resourceFailure(c, BL_OUT_OF_MEMORY, "UI resource identity exhausted");
        return NULL;
    }
    U_Resource* p = (U_Resource*)l_alloc(c->runtime, sizeof(U_Resource));
    if (!p)
    {
        u_resourceFailure(c, BL_OUT_OF_MEMORY, "UI GPU resource allocation failed");
        return NULL;
    }
    p->handle = ++c->resourceSerial;
    p->kind = kind;
    p->vertices = BGFX_INVALID_HANDLE;
    p->indices = BGFX_INVALID_HANDLE;
    p->texture = BGFX_INVALID_HANDLE;
    p->samplerFlags = UINT32_MAX;
    p->next = c->resources;
    c->resources = p;
    return p;
}

uint64_t u_geometry(U_Context* c, const float* vertices, size_t vertexCount,
                    const uint32_t* indices, size_t indexCount)
{
    size_t vertexBytes;
    size_t indexBytes;
    if (!vertices || !indices || !vertexCount || !indexCount || indexCount % 3 ||
        !l_size(vertexCount, sizeof(float) * 8, &vertexBytes) ||
        !l_size(indexCount, sizeof(uint32_t), &indexBytes) || vertexBytes > UINT32_MAX ||
        indexBytes > UINT32_MAX)
    {
        u_resourceFailure(c, BL_INVALID_ARGUMENT, "Invalid RmlUI triangle geometry");
        return 0;
    }
    for (size_t i = 0; i < vertexCount * 8; ++i)
    {
        if (!isfinite(vertices[i]))
        {
            u_resourceFailure(c, BL_INVALID_ARGUMENT, "Non-finite RmlUI vertex data");
            return 0;
        }
    }
    for (size_t i = 0; i < indexCount; ++i)
    {
        if (indices[i] >= vertexCount)
        {
            u_resourceFailure(c, BL_INVALID_ARGUMENT, "RmlUI geometry index exceeds vertex range");
            return 0;
        }
    }
    U_Resource* p = resourceCreate(c, U_GEOMETRY);
    if (!p)
    {
        return 0;
    }
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
    p->vertices = bgfx::createVertexBuffer(bgfx::copy(vertices, (uint32_t)vertexBytes), layout);
    p->indices =
        bgfx::createIndexBuffer(bgfx::copy(indices, (uint32_t)indexBytes), BGFX_BUFFER_INDEX32);
    if (!bgfx::isValid(p->vertices) || !bgfx::isValid(p->indices))
    {
        p->retired = true;
        u_resourceFailure(c, BL_OUT_OF_MEMORY, "bgfx UI geometry allocation failed");
        return 0;
    }
    p->indexCount = (uint32_t)indexCount;
    p->whiteSource = true;
    for (size_t i = 0; i < vertexCount; ++i)
    {
        const float* color = vertices + i * 8 + 2;
        p->whiteSource =
            p->whiteSource && color[0] == color[3] && color[1] == color[3] && color[2] == color[3];
    }
    ++c->stats.geometryCompileCount;
    ++c->stats.liveGeometryCount;
    c->stats.uploadedBytes += vertexBytes + indexBytes;
    return p->handle;
}

uint64_t u_texture(U_Context* c, const uint8_t* pixels, uint32_t width, uint32_t height)
{
    size_t bytes;
    if (!pixels || !width || !height || width > UINT16_MAX || height > UINT16_MAX ||
        width > bgfx::getCaps()->limits.maxTextureSize ||
        height > bgfx::getCaps()->limits.maxTextureSize ||
        !l_size((size_t)width * height, 4, &bytes) || bytes > UINT32_MAX)
    {
        u_resourceFailure(c, BL_INVALID_ARGUMENT, "UI texture dimensions exceed backend limits");
        return 0;
    }
    U_Resource* p = resourceCreate(c, U_TEXTURE);
    if (!p)
    {
        return 0;
    }
    p->texture = bgfx::createTexture2D(
        (uint16_t)width, (uint16_t)height, false, 1, bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, bgfx::copy(pixels, (uint32_t)bytes));
    if (!bgfx::isValid(p->texture))
    {
        p->retired = true;
        u_resourceFailure(c, BL_OUT_OF_MEMORY, "bgfx UI texture allocation failed");
        return 0;
    }
    ++c->stats.textureCreateCount;
    p->whiteSource = true;
    for (size_t i = 0; i < bytes; i += 4)
    {
        p->whiteSource = p->whiteSource && pixels[i] == pixels[i + 3] &&
                         pixels[i + 1] == pixels[i + 3] && pixels[i + 2] == pixels[i + 3];
    }
    ++c->stats.liveTextureCount;
    c->stats.uploadedBytes += bytes;
    return p->handle;
}

uint64_t u_gradient(U_Context* c, const float parameters[18][4])
{
    if (parameters[1][0] < 2 || parameters[1][0] > 8 ||
        parameters[1][0] != floorf(parameters[1][0]))
    {
        u_resourceFailure(c, BL_INVALID_ARGUMENT, "Invalid UI gradient stop count");
        return 0;
    }
    for (size_t i = 0; i < 18; ++i)
    {
        for (size_t j = 0; j < 4; ++j)
        {
            if (!isfinite(parameters[i][j]))
            {
                u_resourceFailure(c, BL_INVALID_ARGUMENT, "Non-finite UI gradient parameters");
                return 0;
            }
        }
    }
    U_Resource* p = resourceCreate(c, U_GRADIENT);
    if (!p)
    {
        return 0;
    }
    memcpy(p->gradient, parameters, sizeof(p->gradient));
    p->whiteSource = true;
    for (size_t i = 0; i < (size_t)parameters[1][0]; ++i)
    {
        const float* color = parameters[10 + i];
        p->whiteSource =
            p->whiteSource && color[0] == color[3] && color[1] == color[3] && color[2] == color[3];
    }
    return p->handle;
}

void u_release(U_Context* c, uint64_t handle, unsigned kind)
{
    U_Resource* p = u_resource(c, handle, kind);
    if (!p)
    {
        return;
    }
    p->retired = true;
    if (kind == U_GEOMETRY)
    {
        ++c->stats.geometryReleaseCount;
        --c->stats.liveGeometryCount;
    }
    else if (kind == U_TEXTURE)
    {
        ++c->stats.textureReleaseCount;
        --c->stats.liveTextureCount;
    }
}

bool u_allocateView(U_Context* c, uint16_t* view)
{
    if (c->viewCursor >= c->views.count)
    {
        u_resourceFailure(c, BL_UNSUPPORTED,
                          "UI multipass render exceeds its reserved view budget");
        return false;
    }
    *view = (uint16_t)(c->views.first + c->viewCursor++);
    return true;
}

void u_begin(U_Context* c)
{
    c->viewCursor = 0;
    c->layerDepth = 0;
    c->stats.drawCount = 0;
    c->clipCommandCount = 0;
    c->scissorEnabled = false;
    c->clipEnabled = false;
    c->clipLevel = 0;
    c->stencilWriting = false;
    memset(c->transform, 0, sizeof(c->transform));
    c->transform[0] = 1;
    c->transform[5] = 1;
    c->transform[10] = 1;
    c->transform[15] = 1;
    u_layerView(c, 0, BGFX_CLEAR_STENCIL, false);
}

static void draw(U_Context* c, U_Resource* g, const float translation[2], U_Resource* t,
                 U_Resource* effect, bgfx::TextureHandle raw, const float* blur, bool replace)
{
    if (c->pending != BL_OK)
    {
        return;
    }
    if (!g)
    {
        return;
    }
    if (c->whiteDifference && !c->stencilWriting &&
        (!g->whiteSource || (t && !t->whiteSource) || (effect && !effect->whiteSource)))
    {
        u_resourceFailure(c, BL_UNSUPPORTED, "White-difference context contains colored sources");
        return;
    }
    U_Program* p = c->programs + (blur ? 3 : effect ? 2 : t || bgfx::isValid(raw) ? 1 : 0);
    float values[108] = {};
    values[0] = 2.0f / (float)c->options.target.width;
    values[5] = -2.0f / (float)c->options.target.height;
    values[10] = 1;
    values[12] = -1;
    values[13] = 1;
    values[15] = 1;
    memcpy(values + 16, c->transform, sizeof(c->transform));
    values[32] = translation[0];
    values[33] = translation[1];
    if (effect)
    {
        memcpy(values + 36, effect->gradient, sizeof(effect->gradient));
    }
    if (blur)
    {
        memcpy(values + 36, blur, sizeof(float) * 4);
    }
    for (size_t i = 0; i < p->uniformCount; ++i)
    {
        memset(p->uniforms[i].bytes, 0, p->uniforms[i].byteCount);
    }
    for (size_t i = 0; i < p->mapCount; ++i)
    {
        const U_UniformMap* m = p->maps + i;
        unsigned offset = m->source < 2 ? m->source * 16 : 32 + (m->source - 2) * 4;
        memcpy(p->uniforms[m->destination].bytes + m->offset, values + offset, m->bytes);
    }
    for (size_t i = 0; i < p->uniformCount; ++i)
    {
        bgfx::setUniform(p->uniforms[i].handle, p->uniforms[i].bytes, p->uniforms[i].count);
    }
    if (c->scissorEnabled)
    {
        int32_t x = c->scissor[0] < 0 ? 0 : c->scissor[0];
        int32_t y = c->scissor[1] < 0 ? 0 : c->scissor[1];
        int32_t right = c->scissor[2] > (int32_t)c->options.target.width
                            ? (int32_t)c->options.target.width
                            : c->scissor[2];
        int32_t bottom = c->scissor[3] > (int32_t)c->options.target.height
                             ? (int32_t)c->options.target.height
                             : c->scissor[3];
        if (right <= x || bottom <= y)
        {
            bgfx::discard();
            return;
        }
        bgfx::setScissor((uint16_t)x, (uint16_t)y, (uint16_t)(right - x), (uint16_t)(bottom - y));
    }
    else
    {
        bgfx::setScissor(UINT16_MAX);
    }
    if (t)
    {
        bgfx::setTexture(p->textureStage, p->sampler, t->texture, t->samplerFlags);
    }
    else if (bgfx::isValid(raw))
    {
        bgfx::setTexture(p->textureStage, p->sampler, raw,
                         BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    }
    bgfx::setVertexBuffer(0, g->vertices);
    bgfx::setIndexBuffer(g->indices, 0, g->indexCount);
    uint64_t state = BGFX_STATE_MSAA;
    if (!c->stencilWriting)
    {
        state |= BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
        if (replace)
        {
            state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ZERO);
        }
        else if (c->whiteDifference)
        {
            state |= BGFX_STATE_BLEND_FUNC_SEPARATE(
                BGFX_STATE_BLEND_INV_DST_COLOR, BGFX_STATE_BLEND_INV_SRC_ALPHA,
                BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);
        }
        else
        {
            state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);
        }
    }
    uint32_t stencil = c->stencilWriting ? c->stencil : BGFX_STENCIL_NONE;
    if (!c->stencilWriting && c->clipEnabled)
    {
        stencil = BGFX_STENCIL_TEST_EQUAL | BGFX_STENCIL_FUNC_REF(c->clipLevel) |
                  BGFX_STENCIL_FUNC_RMASK(0xff) | BGFX_STENCIL_OP_FAIL_S_KEEP |
                  BGFX_STENCIL_OP_FAIL_Z_KEEP | BGFX_STENCIL_OP_PASS_Z_KEEP;
    }
    bgfx::setStencil(stencil);
    bgfx::setState(state);
    bgfx::submit(c->currentView, p->handle);
    ++c->stats.drawCount;
}

void u_draw(U_Context* c, uint64_t geometry, const float translation[2], uint64_t texture,
            uint64_t gradient)
{
    U_Resource* g = u_resource(c, geometry, U_GEOMETRY);
    U_Resource* t = texture ? u_resource(c, texture, U_TEXTURE) : NULL;
    U_Resource* effect = gradient ? u_resource(c, gradient, U_GRADIENT) : NULL;
    if ((texture && !t) || (gradient && !effect))
    {
        return;
    }
    draw(c, g, translation, t, effect, bgfx::TextureHandle{bgfx::kInvalidHandle}, NULL, false);
}

void u_drawRaw(U_Context* c, uint64_t geometry, bgfx::TextureHandle texture,
               const float* blurParameters, bool replace)
{
    U_Resource* g = u_resource(c, geometry, U_GEOMETRY);
    const float zero[] = {0, 0};
    draw(c, g, zero, NULL, NULL, texture, blurParameters, replace);
}

void u_mask(U_Context* c, unsigned operation, uint64_t geometry, const float translation[2])
{
    if (c->options.target.depthFormat != BL_DEPTH_D24S8)
    {
        u_resourceFailure(c, BL_UNSUPPORTED, "Transformed UI clipping requires a D24S8 target");
        return;
    }
    if (operation > 2 || (operation == 2 && c->clipLevel == 254))
    {
        u_resourceFailure(c, BL_UNSUPPORTED, "UI stencil intersection depth exceeds 254");
        return;
    }
    if (!c->replayingClip)
    {
        if (operation < 2)
        {
            c->clipCommandCount = 0;
        }
        if (c->clipCommandCount == U_MAX_CLIP_COMMANDS)
        {
            u_resourceFailure(c, BL_UNSUPPORTED, "UI clip command depth exceeds backend capacity");
            return;
        }
        U_ClipCommand* command = c->clipCommands + c->clipCommandCount++;
        command->geometry = geometry;
        command->operation = operation;
        memcpy(command->translation, translation, sizeof(command->translation));
        memcpy(command->transform, c->transform, sizeof(command->transform));
    }
    c->stencilWriting = true;
    if (operation < 2)
    {
        if (!c->clipQuad)
        {
            float w = (float)c->options.target.width;
            float h = (float)c->options.target.height;
            const float vertices[] = {0, 0, 1, 1, 1, 1, 0, 0, w, 0, 1, 1, 1, 1, 0, 0,
                                      w, h, 1, 1, 1, 1, 0, 0, 0, h, 1, 1, 1, 1, 0, 0};
            const uint32_t indices[] = {0, 1, 2, 0, 2, 3};
            c->clipQuad = u_geometry(c, vertices, 4, indices, 6);
        }
        float transform[16];
        memcpy(transform, c->transform, sizeof(transform));
        memset(c->transform, 0, sizeof(c->transform));
        c->transform[0] = 1;
        c->transform[5] = 1;
        c->transform[10] = 1;
        c->transform[15] = 1;
        bool scissor = c->scissorEnabled;
        c->scissorEnabled = false;
        c->stencil = BGFX_STENCIL_TEST_ALWAYS | BGFX_STENCIL_FUNC_REF(operation == 1 ? 1 : 0) |
                     BGFX_STENCIL_FUNC_RMASK(0xff) | BGFX_STENCIL_OP_FAIL_S_REPLACE |
                     BGFX_STENCIL_OP_FAIL_Z_REPLACE | BGFX_STENCIL_OP_PASS_Z_REPLACE;
        const float zero[] = {0, 0};
        u_draw(c, c->clipQuad, zero, 0, 0);
        memcpy(c->transform, transform, sizeof(transform));
        c->scissorEnabled = scissor;
        c->stencil = BGFX_STENCIL_TEST_ALWAYS | BGFX_STENCIL_FUNC_REF(operation == 1 ? 0 : 1) |
                     BGFX_STENCIL_FUNC_RMASK(0xff) | BGFX_STENCIL_OP_FAIL_S_KEEP |
                     BGFX_STENCIL_OP_FAIL_Z_KEEP | BGFX_STENCIL_OP_PASS_Z_REPLACE;
        c->clipLevel = 1;
    }
    else
    {
        c->stencil = BGFX_STENCIL_TEST_EQUAL | BGFX_STENCIL_FUNC_REF(c->clipLevel) |
                     BGFX_STENCIL_FUNC_RMASK(0xff) | BGFX_STENCIL_OP_FAIL_S_KEEP |
                     BGFX_STENCIL_OP_FAIL_Z_KEEP | BGFX_STENCIL_OP_PASS_Z_INCR;
        ++c->clipLevel;
    }
    u_draw(c, geometry, translation, 0, 0);
    c->stencilWriting = false;
}

void u_replayClip(U_Context* c)
{
    float transform[16];
    memcpy(transform, c->transform, sizeof(transform));
    c->replayingClip = true;
    for (unsigned i = 0; i < c->clipCommandCount; ++i)
    {
        U_ClipCommand* command = c->clipCommands + i;
        memcpy(c->transform, command->transform, sizeof(c->transform));
        u_mask(c, command->operation, command->geometry, command->translation);
    }
    c->replayingClip = false;
    memcpy(c->transform, transform, sizeof(transform));
}

uint64_t u_textureStorage(U_Context* c, uint32_t width, uint32_t height, uint64_t flags)
{
    U_Resource* p = resourceCreate(c, U_TEXTURE);
    if (!p)
    {
        return 0;
    }
    p->texture = bgfx::createTexture2D((uint16_t)width, (uint16_t)height, false, 1,
                                       bgfx::TextureFormat::RGBA8, flags);
    if (!bgfx::isValid(p->texture))
    {
        p->retired = true;
        u_resourceFailure(c, BL_OUT_OF_MEMORY, "UI saved texture allocation failed");
        return 0;
    }
    ++c->stats.textureCreateCount;
    ++c->stats.liveTextureCount;
    return p->handle;
}

uint64_t u_compileBlur(U_Context* c, float sigma)
{
    if (!isfinite(sigma) || sigma < 0 || sigma > 512)
    {
        u_resourceFailure(c, BL_UNSUPPORTED, "UI Gaussian sigma must be in [0,512]");
        return 0;
    }
    U_Resource* p = resourceCreate(c, U_FILTER);
    if (!p)
    {
        return 0;
    }
    p->gradient[0][0] = sigma;
    return p->handle;
}
