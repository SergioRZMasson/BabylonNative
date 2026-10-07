#include "LiteInternal.h"

static void graphicsCleanup(bl_Runtime* r, L_Material* m)
{
    if (m->engine)
    {
        l_unpin(&m->engine->record);
    }
    if (m->programIndex != BL_INVALID_BGFX_HANDLE)
    {
        bgfx::destroy(bgfx::ProgramHandle{m->programIndex});
    }
    m->programIndex = BL_INVALID_BGFX_HANDLE;
    for (size_t i = 0; i < m->nativeUniformCount; ++i)
    {
        L_NativeUniform* u = m->nativeUniforms + i;
        if (bgfx::isValid(u->handle))
        {
            bgfx::destroy(u->handle);
        }
        l_free(r, (void*)u->name.data);
        l_free(r, u->bytes);
    }
    for (size_t i = 0; i < m->textureMapCount; ++i)
    {
        if (bgfx::isValid(m->textureMaps[i].handle))
        {
            bgfx::destroy(m->textureMaps[i].handle);
        }
    }
    l_free(r, m->nativeUniforms);
    l_free(r, m->maps);
    l_free(r, m->textureMaps);
    m->nativeUniforms = NULL;
    m->maps = NULL;
    m->textureMaps = NULL;
    m->nativeUniformCount = 0;
    m->mapCount = 0;
    m->textureMapCount = 0;
    m->activeAttributes = 0;
    m->graphicsCleanup = NULL;
    m->engine = NULL;
}

static bool stages(uint32_t s)
{
    return s && !(s & ~(uint32_t)(BL_STAGE_VERTEX | BL_STAGE_FRAGMENT));
}

static bl_Status textureReady(bl_Runtime* r, L_Engine* e, L_Material* m)
{
    for (size_t i = 0; i < m->textureMapCount; ++i)
    {
        bl_Texture2D h = m->samplers[m->textureMaps[i].slot].texture;
        if (L_NULL(h))
        {
            return L_FAIL(r, BL_NOT_READY, "Active shader texture is unset");
        }
        L_GET(h, L_TEXTURE, L_Texture, t);
        if (t->engine != e)
        {
            return BL_WRONG_ENGINE;
        }
    }
    return BL_OK;
}

static bl_Status reflection(bl_Runtime* r, L_Engine* e, L_Material* m,
                            const bl_ShaderCompileResult* c)
{
    if (!l_typedSpan(c->attributes, c->attributeCount, alignof(bl_ReflectedAttribute)) ||
        !l_typedSpan(c->uniforms, c->uniformCount, alignof(bl_ReflectedUniform)) ||
        !l_typedSpan(c->uniformBlocks, c->uniformBlockCount, alignof(bl_ReflectedUniformBlock)) ||
        !l_typedSpan(c->textures, c->textureCount, alignof(bl_ReflectedTexture)) ||
        c->attributeCount > 6 || c->uniformCount > m->uniformCount * 2 ||
        c->textureCount > m->samplerCount || c->uniformBlockCount > 2)
    {
        return BL_SHADER_ERROR;
    }
    unsigned attrs = 0;
    for (size_t i = 0; i < c->attributeCount; ++i)
    {
        const bl_ReflectedAttribute* a = c->attributes + i;
        unsigned semantic = (unsigned)a->semantic;
        if (!l_string(a->name) || semantic >= 6 || a->location >= m->attributeCount ||
            m->attributes[a->location] != a->semantic ||
            a->components != attributeComponents[semantic] ||
            !l_name(a->name, attributeNames[semantic]) || (attrs & (1u << semantic)))
        {
            return BL_SHADER_ERROR;
        }
        attrs |= 1u << semantic;
    }
    m->activeAttributes = attrs;
    m->nativeUniforms = (L_NativeUniform*)l_alloc(r, c->uniformCount * sizeof(*m->nativeUniforms));
    m->maps = (L_UniformMap*)l_alloc(r, c->uniformCount * sizeof(*m->maps));
    m->textureMaps = (L_TextureMap*)l_alloc(r, c->textureCount * sizeof(*m->textureMaps));
    if ((c->uniformCount && (!m->nativeUniforms || !m->maps)) ||
        (c->textureCount && !m->textureMaps))
    {
        return BL_OUT_OF_MEMORY;
    }
    for (size_t i = 0; i < c->uniformBlockCount; ++i)
    {
        const bl_ReflectedUniformBlock* b = c->uniformBlocks + i;
        uint32_t expected = 0;
        for (size_t j = 0; j < m->uniformCount; ++j)
        {
            L_Uniform* u = m->uniforms + j;
            if (u->system == (b->binding == 0))
            {
                uint32_t end = u->offset + (uint32_t)uniformCounts[u->type] * 4;
                if (end > expected)
                {
                    expected = end;
                }
            }
        }
        if (!expected && b->binding == 0)
        {
            expected = 16;
        }
        expected = (expected + 15) & ~UINT32_C(15);
        if (!l_string(b->name) || b->group != 1 || !b->byteSize || (b->byteSize & 15) ||
            !stages(b->stages) || b->byteSize != expected ||
            !((b->binding == 0 && l_name(b->name, "shaderSystem")) ||
              (b->binding == 1 && l_name(b->name, "shaderUniforms"))))
        {
            return BL_SHADER_ERROR;
        }
    }
    for (size_t i = 0; i < c->uniformCount; ++i)
    {
        const bl_ReflectedUniform* v = c->uniforms + i;
        if (!l_string(v->name))
        {
            return BL_SHADER_ERROR;
        }
        size_t slot = l_uniformSlot(m, v->name);
        if (slot == SIZE_MAX || v->blockIndex >= c->uniformBlockCount || !l_string(v->nativeName) ||
            !v->nativeName.length || v->type != m->uniforms[slot].type ||
            v->byteOffset != m->uniforms[slot].offset ||
            v->byteSize != uniformCounts[v->type] * 4 || !stages(v->stages) ||
            (unsigned)v->nativeType > BL_NATIVE_UNIFORM_MAT4 || !v->nativeCount)
        {
            return BL_SHADER_ERROR;
        }
        const bl_ReflectedUniformBlock* b = c->uniformBlocks + v->blockIndex;
        if (b->binding != (m->uniforms[slot].system ? 0u : 1u) || v->byteOffset > b->byteSize ||
            v->byteSize > b->byteSize - v->byteOffset || (v->stages & ~b->stages))
        {
            return BL_SHADER_ERROR;
        }
        size_t nativeSlot = SIZE_MAX;
        for (size_t j = 0; j < m->nativeUniformCount; ++j)
        {
            if (l_equal(m->nativeUniforms[j].name, v->nativeName))
            {
                nativeSlot = j;
                break;
            }
        }
        if (nativeSlot == SIZE_MAX)
        {
            nativeSlot = m->nativeUniformCount++;
            L_NativeUniform* u = m->nativeUniforms + nativeSlot;
            u->handle.idx = BL_INVALID_BGFX_HANDLE;
            u->type = v->nativeType;
            u->count = v->nativeCount;
            u->byteCount = (v->nativeType == BL_NATIVE_UNIFORM_VEC4   ? 16
                            : v->nativeType == BL_NATIVE_UNIFORM_MAT3 ? 48
                                                                      : 64) *
                           (size_t)v->nativeCount;
            u->name = l_copyString(r, v->nativeName);
            u->bytes = (uint8_t*)l_alloc(r, u->byteCount);
            if (!u->name.data || !u->bytes)
            {
                return BL_OUT_OF_MEMORY;
            }
            bgfx::UniformType::Enum type =
                v->nativeType == BL_NATIVE_UNIFORM_VEC4   ? bgfx::UniformType::Vec4
                : v->nativeType == BL_NATIVE_UNIFORM_MAT3 ? bgfx::UniformType::Mat3
                                                          : bgfx::UniformType::Mat4;
            u->handle = bgfx::createUniform(u->name.data, type, u->count);
            if (!bgfx::isValid(u->handle))
            {
                return BL_OUT_OF_MEMORY;
            }
        }
        L_NativeUniform* u = m->nativeUniforms + nativeSlot;
        if (u->type != v->nativeType || u->count != v->nativeCount ||
            v->nativeByteOffset > u->byteCount ||
            v->byteSize > u->byteCount - v->nativeByteOffset || (v->nativeByteOffset & 3))
        {
            return BL_SHADER_ERROR;
        }
        for (size_t j = 0; j < m->mapCount; ++j)
        {
            L_UniformMap* p = m->maps + j;
            if (p->nativeSlot == nativeSlot && v->nativeByteOffset < p->offset + p->byteSize &&
                p->offset < v->nativeByteOffset + v->byteSize)
            {
                return BL_SHADER_ERROR;
            }
        }
        m->maps[m->mapCount++] = {slot, nativeSlot, v->nativeByteOffset, v->byteSize, v->stages};
    }
    bool custom = false;
    for (size_t i = 0; i < m->uniformCount; ++i)
    {
        if (!m->uniforms[i].system)
        {
            custom = true;
        }
    }
    for (size_t i = 0; i < c->textureCount; ++i)
    {
        const bl_ReflectedTexture* t = c->textures + i;
        if (!l_string(t->name) || !l_string(t->samplerName))
        {
            return BL_SHADER_ERROR;
        }
        size_t slot = l_samplerSlot(m, t->name);
        if (slot == SIZE_MAX || !l_samplerSuffix(t->samplerName, t->name) ||
            !l_string(t->nativeName) || !t->nativeName.length || t->textureGroup != 1 ||
            t->samplerGroup != 1 || t->textureBinding != (custom ? 2u : 1u) + slot * 2 ||
            t->samplerBinding != t->textureBinding + 1 || !stages(t->stages) ||
            t->nativeTextureStage >= bgfx::getCaps()->limits.maxTextureSamplers)
        {
            return BL_SHADER_ERROR;
        }
        for (size_t j = 0; j < m->textureMapCount; ++j)
        {
            if (m->textureMaps[j].stage == t->nativeTextureStage || m->textureMaps[j].slot == slot)
            {
                return BL_SHADER_ERROR;
            }
        }
        bl_String name = l_copyString(r, t->nativeName);
        if (!name.data)
        {
            return BL_OUT_OF_MEMORY;
        }
        bgfx::UniformHandle handle = bgfx::createUniform(name.data, bgfx::UniformType::Sampler);
        l_free(r, (void*)name.data);
        if (!bgfx::isValid(handle))
        {
            return BL_OUT_OF_MEMORY;
        }
        m->textureMaps[m->textureMapCount++] = {slot, handle, t->nativeTextureStage};
    }
    return textureReady(r, e, m);
}

bl_Status l_prepareMaterial(bl_Runtime* r, L_Engine* e, L_Material* m)
{
    if (m->record.disposed)
    {
        return BL_DISPOSED;
    }
    if (m->engine && m->engine != e)
    {
        return BL_WRONG_ENGINE;
    }
    if (m->programIndex != BL_INVALID_BGFX_HANDLE)
    {
        return textureReady(r, e, m);
    }
    if (!r->hasCompiler)
    {
        return L_FAIL(r, BL_NOT_READY, "No runtime WGSL compiler service");
    }
    if (m->bias != 0 || m->slope != 0)
    {
        return L_FAIL(r, BL_UNSUPPORTED, "bgfx public draw state does not expose depth bias");
    }
    L_Text vertex = {};
    L_Text fragment = {};
    bool ok = l_appendText(r, &vertex, m->prelude) && l_appendText(r, &vertex, m->vertex) &&
              l_appendText(r, &fragment, m->prelude) && l_appendText(r, &fragment, m->fragment);
    if (!ok)
    {
        l_free(r, vertex.data);
        l_free(r, fragment.data);
        return BL_OUT_OF_MEMORY;
    }
    const bgfx::Caps* caps = bgfx::getCaps();
    bl_ShaderCompileRequest request = {
        BL_CONTRACT_VERSION,
        e->backend,
        12,
        {BL_STAGE_VERTEX, {vertex.data, vertex.length}, {"mainVertex", 10}},
        {BL_STAGE_FRAGMENT, {fragment.data, fragment.length}, {"mainFragment", 12}},
        caps->homogeneousDepth,
        caps->originBottomLeft,
        m->name};
    bl_ShaderCompileResult result = {};
    bl_Status status;
    L_CALL(r, status, r->compiler.compile(r->compiler.userData, &request, &result));
    m->graphicsCleanup = graphicsCleanup;
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
            status = reflection(r, e, m, &result);
        }
    }
    if (status == BL_OK)
    {
        bgfx::ShaderHandle vs = bgfx::createShader(
            bgfx::copy(result.vertexContainer.data, (uint32_t)result.vertexContainer.count));
        bgfx::ShaderHandle fs = bgfx::createShader(
            bgfx::copy(result.fragmentContainer.data, (uint32_t)result.fragmentContainer.count));
        if (bgfx::isValid(vs) && bgfx::isValid(fs))
        {
            m->programIndex = bgfx::createProgram(vs, fs, true).idx;
        }
        else
        {
            if (bgfx::isValid(vs))
            {
                bgfx::destroy(vs);
            }
            if (bgfx::isValid(fs))
            {
                bgfx::destroy(fs);
            }
        }
        if (m->programIndex == BL_INVALID_BGFX_HANDLE)
        {
            status = BL_SHADER_ERROR;
        }
    }
    char diagnostics[1024] = {};
    if (status != BL_OK && l_string(result.diagnostics) && result.diagnostics.length)
    {
        size_t n = result.diagnostics.length < sizeof(diagnostics) - 1 ? result.diagnostics.length
                                                                       : sizeof(diagnostics) - 1;
        while (n && n < result.diagnostics.length &&
               ((uint8_t)result.diagnostics.data[n] & 0xc0) == 0x80)
        {
            --n;
        }
        memcpy(diagnostics, result.diagnostics.data, n);
    }
    l_enterHost(r);
    r->compiler.release(r->compiler.userData, &result);
    l_leaveHost(r);
    l_free(r, vertex.data);
    l_free(r, fragment.data);
    if (status != BL_OK)
    {
        graphicsCleanup(r, m);
        return L_FAIL(r, status, diagnostics[0] ? diagnostics : "Shader preparation failed");
    }
    m->engine = e;
    l_pin(&e->record);
    return BL_OK;
}

static void matrixValue(float* out, const bl_Mat4* m)
{
    for (unsigned i = 0; i < 16; ++i)
    {
        out[i] = (float)m->values[i];
    }
}

static void systemValues(L_Uniform* u, L_Engine* e, L_Mesh* mesh, const bl_Mat4* view,
                         const bl_Mat4* projection, bl_Vec3 position)
{
    bl_Mat4 temp;
    bl_Mat4 other;
    if (l_name(u->name, "world"))
    {
        matrixValue(u->values, &mesh->node.world);
    }
    else if (l_name(u->name, "view"))
    {
        matrixValue(u->values, view);
    }
    else if (l_name(u->name, "projection"))
    {
        matrixValue(u->values, projection);
    }
    else if (l_name(u->name, "viewProjection"))
    {
        l_multiply(projection, view, &temp, e->highPrecision);
        matrixValue(u->values, &temp);
    }
    else if (l_name(u->name, "worldView"))
    {
        l_multiply(view, &mesh->node.world, &temp, e->highPrecision);
        matrixValue(u->values, &temp);
    }
    else if (l_name(u->name, "worldViewProjection"))
    {
        l_multiply(view, &mesh->node.world, &temp, e->highPrecision);
        l_multiply(projection, &temp, &other, e->highPrecision);
        matrixValue(u->values, &other);
    }
    else if (l_name(u->name, "cameraPosition"))
    {
        u->values[0] = (float)position.x;
        u->values[1] = (float)position.y;
        u->values[2] = (float)position.z;
    }
    else if (l_name(u->name, "screenSize"))
    {
        u->values[0] = (float)e->native.target.width;
        u->values[1] = (float)e->native.target.height;
    }
}

static uint32_t integerBits(float f)
{
    double value = (double)f;
    if (!isfinite(value) || value == 0)
    {
        return 0;
    }
    value = fmod(trunc(value), 4294967296.0);
    if (value < 0)
    {
        value += 4294967296.0;
    }
    return (uint32_t)value;
}

bl_Status l_drawMaterial(bl_Runtime* r, L_Engine* e, L_Material* m, L_Mesh* mesh,
                         const bl_Mat4* view, const bl_Mat4* projection, bl_Vec3 position,
                         uint16_t viewId)
{
    L_TRY(l_prepareMaterial(r, e, m));
    if (m->activeAttributes & ~mesh->geometry.attributeMask)
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Active shader attribute missing from mesh");
    }
    for (size_t i = 0; i < m->nativeUniformCount; ++i)
    {
        memset(m->nativeUniforms[i].bytes, 0, m->nativeUniforms[i].byteCount);
    }
    for (size_t i = 0; i < m->uniformCount; ++i)
    {
        if (m->uniforms[i].system)
        {
            systemValues(m->uniforms + i, e, mesh, view, projection, position);
        }
    }
    for (size_t i = 0; i < m->mapCount; ++i)
    {
        L_UniformMap* p = m->maps + i;
        L_Uniform* u = m->uniforms + p->slot;
        uint8_t* dest = m->nativeUniforms[p->nativeSlot].bytes + p->offset;
        if (u->type == BL_UNIFORM_U32 || u->type == BL_UNIFORM_I32)
        {
            uint32_t bits = integerBits(u->values[0]);
            memcpy(dest, &bits, 4);
        }
        else
        {
            memcpy(dest, u->values, p->byteSize);
        }
    }
    for (size_t i = 0; i < m->nativeUniformCount; ++i)
    {
        L_NativeUniform* u = m->nativeUniforms + i;
        bgfx::setUniform(u->handle, u->bytes, u->count);
    }
    for (size_t i = 0; i < m->textureMapCount; ++i)
    {
        L_TextureMap* p = m->textureMaps + i;
        bl_Texture2D h = m->samplers[p->slot].texture;
        L_Texture* t = (L_Texture*)l_peek(r, h._id);
        bgfx::setTexture(p->stage, p->handle, bgfx::TextureHandle{t->handleIndex});
    }
    const uint64_t compare[9] = {
        BGFX_STATE_DEPTH_TEST_GEQUAL,   BGFX_STATE_DEPTH_TEST_NEVER,  BGFX_STATE_DEPTH_TEST_LESS,
        BGFX_STATE_DEPTH_TEST_EQUAL,    BGFX_STATE_DEPTH_TEST_LEQUAL, BGFX_STATE_DEPTH_TEST_GREATER,
        BGFX_STATE_DEPTH_TEST_NOTEQUAL, BGFX_STATE_DEPTH_TEST_GEQUAL, BGFX_STATE_DEPTH_TEST_ALWAYS};
    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA;
    if (e->native.target.depthFormat != BL_DEPTH_NONE)
    {
        state |= compare[m->depthCompare];
        if (m->depthWrite)
        {
            state |= BGFX_STATE_WRITE_Z;
        }
    }
    if (m->culling)
    {
        state |= BGFX_STATE_CULL_CW;
    }
    if (m->blending)
    {
        state |=
            m->blend == BL_BLEND_ADDITIVE
                ? BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE,
                                                 BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE)
                : BGFX_STATE_BLEND_FUNC_SEPARATE(
                      BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA,
                      BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);
    }
    if (m->topology == BL_TOPOLOGY_LINE_LIST)
    {
        state |= BGFX_STATE_PT_LINES;
    }
    if (m->topology == BL_TOPOLOGY_POINT_LIST)
    {
        state |= BGFX_STATE_PT_POINTS;
    }
    bgfx::setVertexBuffer(0, mesh->geometry.vertexBuffer, 0, (uint32_t)mesh->geometry.vertices);
    bgfx::setIndexBuffer(mesh->geometry.indexBuffer, 0, (uint32_t)mesh->geometry.indexCount);
    bgfx::setState(state);
    bgfx::submit(viewId, bgfx::ProgramHandle{m->programIndex});
    ++e->stats.drawCallCount;
    return BL_OK;
}
