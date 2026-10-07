#include "LiteInternal.h"
#include <stdarg.h>

static const size_t uniformCounts[7] = {1, 1, 1, 2, 3, 4, 16};
static const uint32_t uniformAlign[7] = {4, 4, 4, 8, 16, 16, 16};
static const char* uniformTypes[7] = {"f32",       "u32",       "i32",        "vec2<f32>",
                                      "vec3<f32>", "vec4<f32>", "mat4x4<f32>"};
static const char* attributeNames[6] = {"position", "normal", "uv", "uv2", "tangent", "color"};
static const uint8_t attributeComponents[6] = {3, 3, 2, 2, 4, 4};

static bool systemType(bl_String name, bl_ShaderUniformType* type)
{
    if (l_name(name, "cameraPosition"))
    {
        *type = BL_UNIFORM_VEC3;
    }
    else if (l_name(name, "screenSize"))
    {
        *type = BL_UNIFORM_VEC2;
    }
    else if (l_name(name, "alphaCutoff"))
    {
        *type = BL_UNIFORM_F32;
    }
    else if (l_name(name, "world") || l_name(name, "view") || l_name(name, "projection") ||
             l_name(name, "viewProjection") || l_name(name, "worldView") ||
             l_name(name, "worldViewProjection"))
    {
        *type = BL_UNIFORM_MAT4;
    }
    else
    {
        return false;
    }
    return true;
}

static size_t findUniform(L_Material* m, bl_String name)
{
    for (size_t i = 0; i < m->uniformCount; ++i)
    {
        if (l_equal(m->uniforms[i].name, name))
        {
            return i;
        }
    }
    return SIZE_MAX;
}

static size_t findSampler(L_Material* m, bl_String name)
{
    for (size_t i = 0; i < m->samplerCount; ++i)
    {
        if (l_equal(m->samplers[i].name, name))
        {
            return i;
        }
    }
    return SIZE_MAX;
}

static bool samplerSuffix(bl_String a, bl_String b)
{
    return a.length == b.length + 7 && !memcmp(a.data, b.data, b.length) &&
           !memcmp(a.data + b.length, "Sampler", 7);
}

static bool boolOption(bl_OptionalBool o)
{
    return o >= BL_BOOL_DEFAULT && o <= BL_BOOL_TRUE;
}

struct L_Text
{
    char* data;
    size_t length;
    size_t capacity;
};

static bool appendText(bl_Runtime* r, L_Text* t, bl_String s)
{
    if (s.length > SIZE_MAX - t->length - 1)
    {
        return false;
    }
    size_t need = t->length + s.length + 1;
    if (need > t->capacity)
    {
        size_t cap = t->capacity ? t->capacity : 512;
        while (cap < need)
        {
            if (cap > SIZE_MAX / 2)
            {
                cap = need;
                break;
            }
            cap *= 2;
        }
        char* p = (char*)l_alloc(r, cap);
        if (!p)
        {
            return false;
        }
        if (t->length)
        {
            memcpy(p, t->data, t->length);
        }
        l_free(r, t->data);
        t->data = p;
        t->capacity = cap;
    }
    if (s.length)
    {
        memcpy(t->data + t->length, s.data, s.length);
    }
    t->length += s.length;
    t->data[t->length] = 0;
    return true;
}

static bool text(bl_Runtime* r, L_Text* t, const char* s)
{
    return appendText(r, t, {s, strlen(s)});
}

static bool format(bl_Runtime* r, L_Text* t, const char* fmt, ...)
{
    char p[128];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(p, sizeof(p), fmt, args);
    va_end(args);
    return n >= 0 && (size_t)n < sizeof(p) && appendText(r, t, {p, (size_t)n});
}

static bool prelude(bl_Runtime* r, L_Material* m)
{
    L_Text t = {};
    bool ok = text(r, &t, "struct ShaderSystemUniforms {\n");
    size_t systemCount = 0;
    size_t customCount = 0;
    uint32_t offsets[2] = {};
    for (size_t i = 0; ok && i < m->uniformCount; ++i)
    {
        if (m->uniforms[i].system)
        {
            L_Uniform* u = m->uniforms + i;
            uint32_t alignment = uniformAlign[u->type];
            offsets[0] = (offsets[0] + alignment - 1) & ~(alignment - 1);
            u->offset = offsets[0];
            offsets[0] += (uint32_t)uniformCounts[u->type] * 4;
            ok = appendText(r, &t, u->name) && format(r, &t, ": %s,\n", uniformTypes[u->type]);
            ++systemCount;
        }
    }
    if (!systemCount)
    {
        ok = ok && text(r, &t, "_pad: vec4<f32>,\n");
    }
    ok = ok &&
         text(r, &t, "}\n@group(1) @binding(0) var<uniform> shaderSystem: ShaderSystemUniforms;\n");
    for (size_t i = 0; i < m->uniformCount; ++i)
    {
        if (!m->uniforms[i].system)
        {
            ++customCount;
        }
    }
    if (customCount)
    {
        ok = ok && text(r, &t, "struct ShaderUniforms {\n");
        for (size_t i = 0; ok && i < m->uniformCount; ++i)
        {
            if (!m->uniforms[i].system)
            {
                L_Uniform* u = m->uniforms + i;
                uint32_t alignment = uniformAlign[u->type];
                offsets[1] = (offsets[1] + alignment - 1) & ~(alignment - 1);
                u->offset = offsets[1];
                offsets[1] += (uint32_t)uniformCounts[u->type] * 4;
                ok = appendText(r, &t, u->name) && format(r, &t, ": %s,\n", uniformTypes[u->type]);
            }
        }
        ok = ok &&
             text(r, &t, "}\n@group(1) @binding(1) var<uniform> shaderUniforms: ShaderUniforms;\n");
    }
    uint32_t binding = customCount ? 2 : 1;
    for (size_t i = 0; ok && i < m->samplerCount; ++i)
    {
        ok = format(r, &t, "@group(1) @binding(%u) var ", binding++) &&
             appendText(r, &t, m->samplers[i].name) && text(r, &t, ": texture_2d<f32>;\n") &&
             format(r, &t, "@group(1) @binding(%u) var ", binding++) &&
             appendText(r, &t, m->samplers[i].name) && text(r, &t, "Sampler: sampler;\n");
    }
    for (size_t i = 0; ok && i < m->defineCount; ++i)
    {
        bl_ShaderDefine* d = m->defines + i;
        ok = text(r, &t, "const ") && appendText(r, &t, d->name);
        if (d->isBoolean)
        {
            ok = ok && format(r, &t, ": bool = %s;\n", d->booleanValue ? "true" : "false");
        }
        else
        {
            ok = ok && format(r, &t, ": f32 = %.17e;\n", d->numberValue);
        }
    }
    ok = ok && text(r, &t, "struct VertexInput {\n");
    for (size_t i = 0; ok && i < m->attributeCount; ++i)
    {
        unsigned a = (unsigned)m->attributes[i];
        ok = format(r, &t, "@location(%u) %s: vec%u<f32>,\n", (unsigned)i, attributeNames[a],
                    attributeComponents[a]);
    }
    ok = ok && text(r, &t, "}\n");
    if (!ok)
    {
        l_free(r, t.data);
        return false;
    }
    m->prelude = {t.data, t.length};
    return true;
}

void l_materialCleanup(bl_Runtime* r, L_Record* record)
{
    L_Material* m = (L_Material*)record;
    if (m->graphicsCleanup)
    {
        m->graphicsCleanup(r, m);
    }
    for (size_t i = 0; i < m->uniformCount; ++i)
    {
        l_free(r, (void*)m->uniforms[i].name.data);
    }
    for (size_t i = 0; i < m->samplerCount; ++i)
    {
        if (!L_NULL(m->samplers[i].texture))
        {
            L_Texture* t = (L_Texture*)l_peek(r, m->samplers[i].texture._id);
            if (t && t->references)
            {
                --t->references;
            }
        }
        l_free(r, (void*)m->samplers[i].name.data);
    }
    for (size_t i = 0; i < m->defineCount; ++i)
    {
        l_free(r, (void*)m->defines[i].name.data);
    }
    l_free(r, (void*)m->name.data);
    l_free(r, (void*)m->vertex.data);
    l_free(r, (void*)m->fragment.data);
    l_free(r, (void*)m->prelude.data);
    l_free(r, m->attributes);
    l_free(r, m->uniforms);
    l_free(r, m->samplers);
    l_free(r, m->defines);
    m->uniforms = NULL;
    m->samplers = NULL;
    m->defines = NULL;
    m->uniformCount = m->samplerCount = m->defineCount = 0;
}

bl_Status bl_createShaderMaterial(bl_Runtime* r, const bl_ShaderMaterialOptions* o,
                                  bl_ShaderMaterial* out)
{
    L_TRY(l_check(r));
    if (o && (!o->vertexSource.length || !o->fragmentSource.length))
    {
        return l_error(r, BL_INVALID_ARGUMENT, __func__, "Shader sources required", 389);
    }
    if (!o || !out || !l_string(o->name) || !l_string(o->vertexSource) || !o->vertexSource.length ||
        !l_string(o->fragmentSource) || !o->fragmentSource.length ||
        !l_typedSpan(o->attributes, o->attributeCount, alignof(bl_VertexSemantic)) ||
        !l_typedSpan(o->uniforms, o->uniformCount, alignof(bl_ShaderUniformDecl)) ||
        !l_typedSpan(o->samplers, o->samplerCount, alignof(bl_ShaderSamplerDecl)) ||
        !l_typedSpan(o->defines, o->defineCount, alignof(bl_ShaderDefine)) ||
        !boolOption(o->needAlphaBlending) || !boolOption(o->needAlphaTesting) ||
        !boolOption(o->backFaceCulling) || !boolOption(o->depthWrite) ||
        (unsigned)o->blendMode > BL_BLEND_ADDITIVE ||
        (unsigned)o->depthCompare > BL_COMPARE_ALWAYS ||
        (unsigned)o->topology > BL_TOPOLOGY_POINT_LIST ||
        (o->depthBias.present && !isfinite(o->depthBias.value)) ||
        (o->depthBiasSlopeScale.present && !isfinite(o->depthBiasSlopeScale.value)) ||
        o->attributeCount > 6 || o->uniformCount > UINT32_MAX / 64 || o->samplerCount > 128 ||
        o->defineCount > UINT32_MAX / sizeof(bl_ShaderDefine))
    {
        return L_FAIL(r, BL_INVALID_ARGUMENT, "Invalid material declaration");
    }
    unsigned seen = 0;
    for (size_t i = 0; i < o->attributeCount; ++i)
    {
        unsigned a = (unsigned)o->attributes[i];
        if (a >= 6)
        {
            return l_error(r, BL_INVALID_ARGUMENT, __func__, "Unknown vertex attribute", 391);
        }
        if (seen & (1u << a))
        {
            return l_error(r, BL_INVALID_ARGUMENT, __func__, "Duplicate vertex attribute", 392);
        }
        seen |= 1u << a;
    }
    if (!(seen & 1))
    {
        return l_error(r, BL_INVALID_ARGUMENT, __func__, "Position attribute required", 393);
    }
    for (size_t i = 0; i < o->uniformCount; ++i)
    {
        const bl_ShaderUniformDecl* u = o->uniforms + i;
        bl_ShaderUniformType type = u->type;
        if (!l_identifier(u->name))
        {
            return l_error(r, BL_INVALID_ARGUMENT, __func__, "Invalid WGSL uniform identifier",
                           388);
        }
        if (u->system && !systemType(u->name, &type))
        {
            return l_error(r, BL_INVALID_ARGUMENT, __func__, "Unknown system uniform", 396);
        }
        if ((unsigned)type > BL_UNIFORM_MAT4)
        {
            return l_error(r, BL_INVALID_ARGUMENT, __func__, "Unknown uniform type", 397);
        }
        if (!l_typedSpan(u->defaultValue.data, u->defaultValue.count, alignof(double)) ||
            (u->defaultValue.count && u->defaultValue.count != uniformCounts[type]))
        {
            return l_error(r, BL_INVALID_ARGUMENT, __func__, "Uniform value count mismatch", 399);
        }
        for (size_t j = 0; j < u->defaultValue.count; ++j)
        {
            if (!isfinite(u->defaultValue.data[j]))
            {
                return BL_INVALID_ARGUMENT;
            }
        }
        for (size_t j = 0; j < i; ++j)
        {
            if (l_equal(u->name, o->uniforms[j].name))
            {
                return l_error(r, BL_INVALID_ARGUMENT, __func__, "Duplicate uniform", 398);
            }
        }
    }
    for (size_t i = 0; i < o->samplerCount; ++i)
    {
        bl_String n = o->samplers[i].name;
        if (!l_identifier(n))
        {
            return BL_INVALID_ARGUMENT;
        }
        for (size_t j = 0; j < o->uniformCount; ++j)
        {
            if (l_equal(n, o->uniforms[j].name) || samplerSuffix(o->uniforms[j].name, n))
            {
                return BL_INVALID_ARGUMENT;
            }
        }
        for (size_t j = 0; j < i; ++j)
        {
            if (l_equal(n, o->samplers[j].name) || samplerSuffix(n, o->samplers[j].name) ||
                samplerSuffix(o->samplers[j].name, n))
            {
                return BL_INVALID_ARGUMENT;
            }
        }
    }
    for (size_t i = 0; i < o->defineCount; ++i)
    {
        bl_String n = o->defines[i].name;
        if (!l_identifier(n) || (!o->defines[i].isBoolean && !isfinite(o->defines[i].numberValue)))
        {
            return BL_INVALID_ARGUMENT;
        }
        for (size_t j = 0; j < i; ++j)
        {
            if (l_equal(n, o->defines[j].name))
            {
                return BL_INVALID_ARGUMENT;
            }
        }
        for (size_t j = 0; j < o->uniformCount; ++j)
        {
            if (l_equal(n, o->uniforms[j].name))
            {
                return BL_INVALID_ARGUMENT;
            }
        }
        for (size_t j = 0; j < o->samplerCount; ++j)
        {
            if (l_equal(n, o->samplers[j].name) || samplerSuffix(n, o->samplers[j].name))
            {
                return BL_INVALID_ARGUMENT;
            }
        }
    }
    L_NEW(r, L_MATERIAL, 20, l_materialCleanup, L_Material, m);
    m->program.idx = BL_INVALID_BGFX_HANDLE;
    m->attributes = (bl_VertexSemantic*)l_alloc(r, o->attributeCount * sizeof(*m->attributes));
    m->uniforms = (L_Uniform*)l_alloc(r, o->uniformCount * sizeof(*m->uniforms));
    m->samplers = (L_Sampler*)l_alloc(r, o->samplerCount * sizeof(*m->samplers));
    m->defines = (bl_ShaderDefine*)l_alloc(r, o->defineCount * sizeof(*m->defines));
    bool ok = m->attributes && (!o->uniformCount || m->uniforms) &&
              (!o->samplerCount || m->samplers) && (!o->defineCount || m->defines);
    m->name = l_copyString(r, o->name);
    m->vertex = l_copyString(r, o->vertexSource);
    m->fragment = l_copyString(r, o->fragmentSource);
    ok = ok && m->name.data && m->vertex.data && m->fragment.data;
    if (ok)
    {
        memcpy(m->attributes, o->attributes, o->attributeCount * sizeof(*m->attributes));
        m->attributeCount = o->attributeCount;
        for (size_t i = 0; i < o->uniformCount; ++i)
        {
            L_Uniform* u = m->uniforms + i;
            const bl_ShaderUniformDecl* d = o->uniforms + i;
            u->name = l_copyString(r, d->name);
            u->type = d->type;
            u->system = d->system;
            if (u->system)
            {
                systemType(u->name, &u->type);
            }
            ++m->uniformCount;
            if (!u->name.data)
            {
                ok = false;
                break;
            }
            if (!d->defaultValue.count && l_name(u->name, "alphaCutoff"))
            {
                u->values[0] = .4f;
            }
            for (size_t j = 0; j < d->defaultValue.count; ++j)
            {
                u->values[j] = (float)d->defaultValue.data[j];
            }
        }
        for (size_t i = 0; ok && i < o->samplerCount; ++i)
        {
            m->samplers[i].name = l_copyString(r, o->samplers[i].name);
            ++m->samplerCount;
            ok = m->samplers[i].name.data != NULL;
        }
        for (size_t i = 0; ok && i < o->defineCount; ++i)
        {
            m->defines[i] = o->defines[i];
            m->defines[i].name = l_copyString(r, o->defines[i].name);
            ++m->defineCount;
            ok = m->defines[i].name.data != NULL;
        }
        for (size_t i = 1; ok && i < m->defineCount; ++i)
        {
            bl_ShaderDefine v = m->defines[i];
            size_t j = i;
            while (j && strcmp(m->defines[j - 1].name.data, v.name.data) > 0)
            {
                m->defines[j] = m->defines[j - 1];
                --j;
            }
            m->defines[j] = v;
        }
    }
    m->blending = o->needAlphaBlending == BL_BOOL_TRUE;
    m->testing = o->needAlphaTesting == BL_BOOL_TRUE;
    m->culling = o->backFaceCulling != BL_BOOL_FALSE;
    m->depthWrite = o->depthWrite == BL_BOOL_DEFAULT ? !m->blending : o->depthWrite == BL_BOOL_TRUE;
    m->blend = o->blendMode;
    m->depthCompare =
        o->depthCompare == BL_COMPARE_DEFAULT ? BL_COMPARE_GREATER_EQUAL : o->depthCompare;
    m->topology = o->topology;
    m->bias = o->depthBias.present ? o->depthBias.value : 0;
    m->slope = o->depthBiasSlopeScale.present ? o->depthBiasSlopeScale.value : 0;
    if (ok)
    {
        ok = prelude(r, m);
    }
    if (!ok)
    {
        l_materialCleanup(r, &m->record);
        m->record.disposed = true;
        return L_FAIL(r, BL_OUT_OF_MEMORY, "Material allocation failed");
    }
    *out = {r, m->record.id};
    return BL_OK;
}

static bl_Status setUniform(bl_ShaderMaterial h, bl_String name, const void* p, size_t count,
                            bool f32)
{
    L_GET(h, L_MATERIAL, L_Material, m);
    if (!l_string(name) || !l_typedSpan(p, count, f32 ? alignof(float) : alignof(double)))
    {
        return BL_INVALID_ARGUMENT;
    }
    size_t i = findUniform(m, name);
    if (i == SIZE_MAX)
    {
        return l_error(h._runtime, BL_INVALID_ARGUMENT, __func__, "Unknown uniform", 400);
    }
    L_Uniform* u = m->uniforms + i;
    if (count != uniformCounts[u->type])
    {
        return l_error(h._runtime, BL_INVALID_ARGUMENT, __func__, "Uniform value count mismatch",
                       399);
    }
    float temp[16];
    for (size_t j = 0; j < count; ++j)
    {
        double v = f32 ? ((const float*)p)[j] : ((const double*)p)[j];
        if (!isfinite(v))
        {
            return BL_INVALID_ARGUMENT;
        }
        temp[j] = (float)v;
    }
    memcpy(u->values, temp, count * sizeof(float));
    return BL_OK;
}

bl_Status bl_setShaderUniform(bl_ShaderMaterial h, bl_String n, bl_NumberSpan v)
{
    return setUniform(h, n, v.data, v.count, false);
}

bl_Status bl_setShaderUniformF32(bl_ShaderMaterial h, bl_String n, bl_F32Span v)
{
    return setUniform(h, n, v.data, v.count, true);
}

bl_Status bl_getShaderUniform(bl_ShaderMaterial h, bl_String name, bl_ShaderUniformView* out)
{
    L_GET(h, L_MATERIAL, L_Material, m);
    if (!out || !l_string(name))
    {
        return BL_INVALID_ARGUMENT;
    }
    size_t i = findUniform(m, name);
    if (i == SIZE_MAX)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = {m->uniforms[i].type, {m->uniforms[i].values, uniformCounts[m->uniforms[i].type]}};
    return BL_OK;
}

bl_Status bl_setShaderFloat(bl_ShaderMaterial h, bl_String n, double v)
{
    return bl_setShaderUniform(h, n, {&v, 1});
}

bl_Status bl_setShaderVector3(bl_ShaderMaterial h, bl_String n, bl_Vec3 v)
{
    double a[3] = {v.x, v.y, v.z};
    return bl_setShaderUniform(h, n, {a, 3});
}

bl_Status bl_setShaderMatrix(bl_ShaderMaterial h, bl_String n, const bl_Mat4* v)
{
    if (!v)
    {
        return BL_INVALID_ARGUMENT;
    }
    return bl_setShaderUniform(h, n, {v->values, 16});
}

bl_Status bl_setShaderMatrixF32(bl_ShaderMaterial h, bl_String n, bl_F32Span v)
{
    if (v.count != 16)
    {
        return BL_INVALID_ARGUMENT;
    }
    return bl_setShaderUniformF32(h, n, v);
}

bl_Status bl_setShaderTexture(bl_ShaderMaterial h, bl_String name, bl_Texture2D texture)
{
    L_GET(h, L_MATERIAL, L_Material, m);
    if (!l_string(name))
    {
        return BL_INVALID_ARGUMENT;
    }
    size_t slot = findSampler(m, name);
    if (slot == SIZE_MAX)
    {
        return BL_INVALID_ARGUMENT;
    }
    L_Texture* next = NULL;
    if (!L_NULL(texture))
    {
        if (texture._runtime != h._runtime)
        {
            return BL_WRONG_RUNTIME;
        }
        L_Record* record;
        L_TRY(l_get(h._runtime, texture._id, L_TEXTURE, &record));
        next = (L_Texture*)record;
        if (m->engine && next->engine != m->engine)
        {
            return BL_WRONG_ENGINE;
        }
    }
    bl_Texture2D old = m->samplers[slot].texture;
    if (old._id == texture._id)
    {
        return BL_OK;
    }
    if (!L_NULL(old))
    {
        L_Texture* previous = (L_Texture*)l_peek(h._runtime, old._id);
        if (previous && previous->references)
        {
            --previous->references;
        }
    }
    if (next)
    {
        ++next->references;
    }
    m->samplers[slot].texture = texture;
    return BL_OK;
}

bl_Status bl_getShaderTexture(bl_ShaderMaterial h, bl_String name, bl_Texture2D* out)
{
    L_GET(h, L_MATERIAL, L_Material, m);
    if (!out || !l_string(name))
    {
        return BL_INVALID_ARGUMENT;
    }
    size_t slot = findSampler(m, name);
    if (slot == SIZE_MAX)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = m->samplers[slot].texture;
    return BL_OK;
}

bl_Status bl_disposeShaderMaterial(bl_ShaderMaterial h)
{
    L_RETIRED(h, L_MATERIAL, L_Material, m);
    if (!m || m->record.disposed)
    {
        return BL_OK;
    }
    if (m->meshReferences)
    {
        return BL_BUSY;
    }
    l_materialCleanup(h._runtime, &m->record);
    m->record.disposed = true;
    return BL_OK;
}

static void graphicsCleanup(bl_Runtime* r, L_Material* m)
{
    if (m->engine)
    {
        l_unpin(&m->engine->record);
    }
    if (bgfx::isValid(m->program))
    {
        bgfx::destroy(m->program);
    }
    m->program.idx = BL_INVALID_BGFX_HANDLE;
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
    m->nativeUniformCount = m->mapCount = m->textureMapCount = 0;
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
        if (semantic >= 6 || a->location >= m->attributeCount ||
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
        if (b->group != 1 || !b->byteSize || (b->byteSize & 15) || !stages(b->stages) ||
            b->byteSize != expected ||
            !((b->binding == 0 && l_name(b->name, "shaderSystem")) ||
              (b->binding == 1 && l_name(b->name, "shaderUniforms"))))
        {
            return BL_SHADER_ERROR;
        }
    }
    for (size_t i = 0; i < c->uniformCount; ++i)
    {
        const bl_ReflectedUniform* v = c->uniforms + i;
        size_t slot = findUniform(m, v->name);
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
        size_t ns = SIZE_MAX;
        for (size_t j = 0; j < m->nativeUniformCount; ++j)
        {
            if (l_equal(m->nativeUniforms[j].name, v->nativeName))
            {
                ns = j;
                break;
            }
        }
        if (ns == SIZE_MAX)
        {
            ns = m->nativeUniformCount++;
            L_NativeUniform* u = m->nativeUniforms + ns;
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
        L_NativeUniform* u = m->nativeUniforms + ns;
        if (u->type != v->nativeType || u->count != v->nativeCount ||
            v->nativeByteOffset > u->byteCount ||
            v->byteSize > u->byteCount - v->nativeByteOffset || (v->nativeByteOffset & 3))
        {
            return BL_SHADER_ERROR;
        }
        for (size_t j = 0; j < m->mapCount; ++j)
        {
            L_UniformMap* p = m->maps + j;
            if (p->nativeSlot == ns && v->nativeByteOffset < p->offset + p->byteSize &&
                p->offset < v->nativeByteOffset + v->byteSize)
            {
                return BL_SHADER_ERROR;
            }
        }
        m->maps[m->mapCount++] = {slot, ns, v->nativeByteOffset, v->byteSize, v->stages};
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
        size_t slot = findSampler(m, t->name);
        if (slot == SIZE_MAX || !samplerSuffix(t->samplerName, t->name) ||
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
        bgfx::UniformHandle h = bgfx::createUniform(name.data, bgfx::UniformType::Sampler);
        l_free(r, (void*)name.data);
        if (!bgfx::isValid(h))
        {
            return BL_OUT_OF_MEMORY;
        }
        m->textureMaps[m->textureMapCount++] = {slot, h, t->nativeTextureStage};
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
    if (bgfx::isValid(m->program))
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
    bool ok = appendText(r, &vertex, m->prelude) && appendText(r, &vertex, m->vertex) &&
              appendText(r, &fragment, m->prelude) && appendText(r, &fragment, m->fragment);
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
            m->program = bgfx::createProgram(vs, fs, true);
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
        if (!bgfx::isValid(m->program))
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
        bgfx::setTexture(p->stage, p->handle, t->handle);
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
    bgfx::submit(viewId, m->program);
    ++e->stats.drawCallCount;
    return BL_OK;
}
